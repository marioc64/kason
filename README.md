<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/kason-logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/kason-logo.png">
    <img src="docs/assets/kason-logo.png" alt="KaSON logo" width="640">
  </picture>
</p>

# KaSON

KaSON—short for **Kasolik's JSON Parser**—is a small C99 JSON parser designed for
fast, allocation-free
processing. It reports slices of the input buffer instead of building a DOM,
and lets the application decide how each object or array should be handled:
enter it, capture it, skip it, or stop parsing.

The repository also provides:

- a bounded-buffer API for data that is not NUL-terminated;
- a chunked streaming parser with caller-owned scratch memory;
- selective lookup of top-level object fields;
- conversion helpers for strings and numbers;
- a schema layer for unpacking JSON directly into C structs and packing structs
  back to JSON.

KaSON performs no heap allocation. All returned values are views into the
input buffer, and all auxiliary storage is supplied by the caller.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The core library can also be compiled directly:

```sh
cc -std=c99 -O3 app.c kason.c -o app
```

For schema support, add `kason_schema.c`:

```sh
cc -std=c99 -O3 app.c kason.c kason_schema.c -o app
```

### Use from CMake

After installing KaSON, or adding it with `add_subdirectory`:

```cmake
find_package(kason CONFIG REQUIRED)

target_link_libraries(my_parser PRIVATE kason::kason)
# Or, when using the schema API:
target_link_libraries(my_config_loader PRIVATE kason::schema)
```

Available build options include:

| Option | Default | Purpose |
| --- | --- | --- |
| `KaSON_BUILD_TESTS` | on for a top-level build | Unit and conformance tests |
| `KaSON_BUILD_EXAMPLES` | on for a top-level build | Example programs |
| `KaSON_BUILD_BENCHMARK` | off | Comparison benchmark |
| `KaSON_BUILD_FUZZER` | off | Clang libFuzzer target |
| `KaSON_ENABLE_SANITIZERS` | off | AddressSanitizer and UndefinedBehaviorSanitizer |
| `KaSON_BUILD_DOCS` | off | Searchable Doxygen API documentation |
| `BUILD_SHARED_LIBS` | off | Build shared rather than static libraries |

### Format conversion examples

When `KaSON_BUILD_EXAMPLES` is enabled, the build also creates `json_to_yaml`,
`json_pretty`, and `json_compact`. Each program reads standard input and writes
standard output when invoked without arguments. An optional first argument is
the input file; an optional second argument is the output file.

```sh
printf '%s' '{"answer":42}' | build/json_to_yaml
build/json_pretty input.json
build/json_compact input.json output.json
build/json_to_yaml input.json output.yaml
```

## Action-based parsing

`kason_parse()` parses a NUL-terminated buffer. `kason_parse_range()` parses an
inclusive `[begin, end]` range and is preferable when the input length is
already known.

The callback receives a `KaSON_STREAM_EVENT_CONTAINER_BEGIN` event as soon as an
object or array starts. Its return value chooses what happens next:

| Action | Result |
| --- | --- |
| `KaSON_ACTION_ENTER` | Visit immediate children, then receive a container-end event |
| `KaSON_ACTION_CAPTURE` | Validate silently, then receive the complete container slice |
| `KaSON_ACTION_SKIP` | Validate silently and emit no end callback |
| `KaSON_ACTION_BREAK` | Stop successfully |

Primitive values use `KaSON_STREAM_EVENT_VALUE`; their callback returns
`KaSON_CALLBACK_CONTINUE` or `KaSON_CALLBACK_BREAK`.

### Visit values recursively

The root object is a container too, so the callback must return
`KaSON_ACTION_ENTER` for it before its fields can be visited.

```c
#include <stdio.h>
#include <string.h>

#include "kason.h"

static int key_is(const kason_key *key, const char *text)
{
    size_t length;

    if (key == NULL)
        return 0;
    length = (size_t)(key->end - key->begin + 1);
    return strlen(text) == length && memcmp(key->begin, text, length) == 0;
}

static int visit(kason_key *key, kason_data *data, int count, void *user_data)
{
    int *answer = user_data;
    (void)count;

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN)
        return KaSON_ACTION_ENTER;

    if (data->event == KaSON_STREAM_EVENT_VALUE &&
            data->type == KaSON_TYPE_NUMBER && key_is(key, "answer")) {
        if (kason_value_to_int(data->type, data->begin, data->end, answer) !=
                KaSON_CONVERT_SUCCESS)
            return KaSON_CALLBACK_BREAK;
    }
    return KaSON_CALLBACK_CONTINUE;
}

int main(void)
{
    char json[] =
        "{\"name\":\"demo\",\"settings\":{\"answer\":42}}";
    int answer = 0;
    int result = kason_parse(json, visit, &answer);

    if (result != KaSON_PARSE_RESULT_SUCCESS)
        return 1;
    printf("answer = %d\n", answer);
    return 0;
}
```

Object keys and string values are JSON string slices without their surrounding
quotes. Escapes have not been decoded. For escaped keys, compare decoded slices
with `kason_strcmp()` or decode with `kason_strcpy()`.

### Capture one subtree

`KaSON_ACTION_CAPTURE` avoids callbacks for every value inside a selected
container. The corresponding end event contains its complete JSON slice,
including `{}` or `[]`.

```c
static int capture_payload(kason_key *key, kason_data *data,
                           int count, void *user_data)
{
    kason_data *payload = user_data;
    (void)count;

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN) {
        if (key_is(key, "payload"))
            return KaSON_ACTION_CAPTURE;
        return KaSON_ACTION_ENTER;
    }

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_END &&
            key_is(key, "payload")) {
        *payload = *data; /* view into the original input buffer */
    }
    return KaSON_CALLBACK_CONTINUE;
}
```

The captured slice can later be traversed with `kason_parse_container()`:

```c
kason_data payload;

if (kason_parse(json, capture_payload, &payload) == KaSON_PARSE_RESULT_SUCCESS)
    kason_parse_container(&payload, visit, &answer);
```

To ignore an unwanted subtree, return `KaSON_ACTION_SKIP` instead. It is still
syntax-checked, but none of its contents are reported.

### Parse a bounded buffer

The range endpoint is inclusive. Handle an empty input before forming it.

```c
int parse_buffer(char *buffer, size_t length,
                 kason_parse_callback callback, void *user_data)
{
    if (length == 0)
        return KaSON_PARSE_RESULT_INCOMPLETE;
    return kason_parse_range(buffer, buffer + length - 1,
                            callback, user_data);
}
```

## Select selected top-level fields

For repeated extraction from wide objects, build a caller-owned lookup table
once and use `kason_parse_selected()`. The parser validates the whole document
but invokes the callback only for matching top-level keys.

```c
static int selected(const kason_lookup_key *key, kason_data *data,
                    int count, void *user_data)
{
    (void)count;
    (void)user_data;
    printf("%.*s = %.*s\n", key->length, key->value,
           (int)(data->end - data->begin + 1), data->begin);
    return KaSON_CALLBACK_CONTINUE;
}

void read_selected(char *json)
{
    kason_lookup_key slots[8];
    kason_lookup_table table;

    kason_lookup_table_init(&table, slots, 8);
    kason_lookup_table_add(&table, "name", 4);
    kason_lookup_table_add(&table, "status", 6);
    kason_parse_selected(json, &table, selected, NULL);
}
```

The table capacity must leave room for open-addressing collisions. For only one
or two fields, a normal parse callback with direct key comparisons may be
simpler and faster.

## Decode directly into a C struct

The schema layer maps object fields to fixed-layout C structures without a DOM
or heap allocation. Required fields, defaults, nested structs, fixed-capacity
arrays, UTF-8/UTF-16/UTF-32 strings, and JSON output are supported.

```c
#include <stdint.h>
#include <stdio.h>

#include "kason_schema.h"

typedef struct {
    char host[64];
    uint32_t port;
} network_config;

typedef struct {
    char name[32];
    network_config network;
    double scale;
} device_config;

static const kason_schema_field network_fields[] = {
    KaSON_FIELD_STRING(network_config, host, "host", KaSON_REQUIRED),
    KaSON_FIELD_U32(network_config, port, "port", KaSON_DEFAULT_U32(1883))
};
KaSON_SCHEMA_DEFINE(network_schema, network_config, network_fields, 4);

static const kason_schema_field device_fields[] = {
    KaSON_FIELD_STRING(device_config, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_STRUCT(device_config, network, "network", &network_schema,
                      KaSON_REQUIRED),
    KaSON_FIELD_DOUBLE(device_config, scale, "scale", KaSON_DEFAULT_DOUBLE(1.0))
};
KaSON_SCHEMA_DEFINE(device_schema, device_config, device_fields, 8);

int main(void)
{
    char json[] =
        "{\"name\":\"sensor-1\",\"network\":{\"host\":\"broker\"}}";
    char output[256];
    device_config config;
    kason_schema_error error;
    kason_writer writer;

    if (kason_schema_init(&network_schema) != KaSON_SCHEMA_SUCCESS ||
            kason_schema_init(&device_schema) != KaSON_SCHEMA_SUCCESS)
        return 1;

    if (kason_unpack(json, &device_schema, &config, &error) !=
            KaSON_SCHEMA_SUCCESS)
        return 1;

    printf("%s connects to %s:%u\n", config.name, config.network.host,
           (unsigned)config.network.port);

    if (kason_writer_init_buffer(&writer, output, sizeof(output)) !=
            KaSON_SCHEMA_SUCCESS ||
            kason_pack(&writer, &device_schema, &config,
                      KaSON_PACK_OMIT_DEFAULTS, &error) != KaSON_SCHEMA_SUCCESS)
        return 1;

    puts(output);
    return 0;
}
```

Initialize each child schema before its parent. Schema lookup arrays are
embedded by `KaSON_SCHEMA_DEFINE`, so no dynamic allocation is needed. See
`examples/schema_config.c` for the complete example.

## Chunked streaming

`kason_stream` is intended for JSON arriving in chunks. It is a fragment API,
separate from the action-based `kason_parse()` interface:

- primitive values are emitted as complete value events;
- objects and arrays are emitted as begin, part, and end fragments;
- tokens crossing chunk boundaries use caller-owned scratch storage;
- callback pointers are valid only for the duration of the callback.

```c
static int on_fragment(kason_key *key, kason_stream_data *data, void *user_data)
{
    (void)key;
    (void)user_data;

    if (data->event == KaSON_STREAM_EVENT_VALUE)
        printf("value: %.*s\n", (int)(data->end - data->begin + 1),
               data->begin);
    return KaSON_CALLBACK_CONTINUE;
}

int receive_json(char *first, int first_length,
                 char *second, int second_length)
{
    char scratch[256];
    kason_stream stream;
    int result;

    result = kason_stream_init(&stream, scratch, sizeof(scratch),
                              on_fragment, NULL);
    if (result != KaSON_PARSE_RESULT_SUCCESS)
        return result;
    if ((result = kason_stream_feed(&stream, first, first_length)) !=
            KaSON_PARSE_RESULT_SUCCESS)
        return result;
    if ((result = kason_stream_feed(&stream, second, second_length)) !=
            KaSON_PARSE_RESULT_SUCCESS)
        return result;
    return kason_stream_finish(&stream);
}
```

Scratch capacity must accommodate any key or primitive token that crosses a
chunk boundary, escaped tokens that require assembly, and nested-container
tracking. If a complete input buffer is available and semantic traversal is
needed, prefer `kason_parse_range()` and its ENTER/CAPTURE/SKIP actions.

## Value helpers

Number slices can be converted without first copying or NUL-terminating them:

```c
int64_t id;
double temperature;

kason_value_to_int64(data->type, data->begin, data->end, &id);
kason_value_to_double(data->type, data->begin, data->end, &temperature);
```

Integer conversions reject JSON numbers containing a fraction or exponent.
All conversion functions leave the output unchanged on failure and return one
of `KaSON_CONVERT_SUCCESS`, `KaSON_CONVERT_ERROR`, `KaSON_CONVERT_RANGE`, or
`KaSON_CONVERT_TYPE_ERROR`.

String slices are decoded on demand:

```c
char decoded[64];
int length = kason_strcpy(data->begin, data->end,
                         decoded, sizeof(decoded));

if (length >= 0 && length < (int)sizeof(decoded))
    decoded[length] = '\0';
```

Use `kason_strlen()` to calculate the decoded UTF-8 size first, or pass a null
output buffer to `kason_strcpy()`. UTF-16 and UTF-32 variants are also available.

## Memory and lifetime

- Core parsing performs no heap allocation and uses bounded stack state.
- Schema storage, lookup tables, output buffers, and stream scratch are owned by
  the caller.
- Keys and values are inclusive pointer ranges into the input buffer.
- A saved slice remains valid only while its input buffer remains valid and
  unchanged.
- The maximum nesting depth defaults to `KaSON_MAX_NESTING == 128` and can be
  overridden at compile time.

`KaSON_PARSE_RESULT_SUCCESS` is zero. Parse errors have a nonzero major result;
use `KaSON_PARSE_RESULT_MAJOR_MASK` and `KaSON_PARSE_RESULT_MINOR_MASK` when an
application needs to distinguish error classes.

## Performance snapshot

The current comparison benchmark processes each input 100 times. These figures
are a representative development-machine snapshot, not portable guarantees:

| Parser/API | Case | MiB/s | RSS delta |
| --- | --- | ---: | ---: |
| `kason-range` | flat | 875.34 | 48 KiB |
| `kason-range` | nested | 705.42 | 48 KiB |
| `kason-range` | lookup | 796.25 | 48 KiB |
| `kason-selected` | lookup | 441.89 | 48 KiB |
| `kason-schema` | struct | 441.62 | 48 KiB |
| `kason-schema-fast` | struct | 814.29 | 48 KiB |
| `kason-stream` | flat | 298.03 | 64 KiB |
| `kason-stream` | nested | 177.11 | 96 KiB |

In the same run, the fastest DOM parsers were faster on some cases, but used
roughly 2.3–4.6 MiB of additional RSS; traditional DOM implementations used
about 4–14 MiB. KaSON's principal trade-off is direct, allocation-free
processing with predictable application-owned storage.

Build and run the benchmark with:

```sh
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DKaSON_BUILD_BENCHMARK=ON
cmake --build build-bench
./build-bench/bench_kason
```

Optional comparison libraries are detected by CMake. Results vary with the
compiler, CPU, input shape, callback work, and enabled dependencies.

## API documentation

The public headers contain the API reference, while
[`docs/api-guide.md`](docs/api-guide.md) explains API selection, event
semantics, ownership, errors, and thread safety. Generate searchable HTML with
Doxygen installed:

```sh
./docs/generate.sh
```

Open `build-docs/docs/html/index.html`. Documentation is opt-in, so Doxygen is
not required for normal library builds. Set `KaSON_DOCS_BUILD_DIR` to use a
different output directory.

## Tests and fuzzing

The normal test suite covers the parser, streaming, schema, safety, and JSON
conformance cases:

```sh
ctest --test-dir build --output-on-failure
```

To run with sanitizers:

```sh
cmake -S . -B build-asan \
  -DKaSON_ENABLE_SANITIZERS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

With Clang, build the libFuzzer target using:

```sh
cmake -S . -B build-fuzz \
  -DCMAKE_C_COMPILER=clang \
  -DKaSON_BUILD_FUZZER=ON
cmake --build build-fuzz
./build-fuzz/fuzz_kason tests/fuzz_corpus
```

## License

KaSON is released under the MIT License. See `LICENSE`.
