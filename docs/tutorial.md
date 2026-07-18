# KaSON API tutorial

KaSON offers three main ways to work with JSON:

| API | Use it when |
| --- | --- |
| Regular API | The complete JSON document is already in memory |
| Stream API | JSON arrives in several chunks, for example from a socket |
| Schema API | JSON should be converted directly to or from a C structure |

All three APIs avoid heap allocation. The input buffers, stream scratch space,
structures, and output buffers belong to your program.

## 1. Regular API

The regular API parses a complete, NUL-terminated JSON string with
`kason_parse()`. KaSON calls your callback for each value. When it encounters an
object or array, the callback returns `KaSON_ACTION_ENTER` to visit its contents.

This example reads the `name` and `age` fields:

```c
#include <stdio.h>
#include <string.h>

#include "kason.h"

static int key_is(const kason_key *key, const char *expected)
{
    size_t length;

    if (key == NULL)
        return 0;
    length = (size_t)(key->end - key->begin + 1);
    return strlen(expected) == length &&
           memcmp(key->begin, expected, length) == 0;
}

static int on_value(kason_key *key, kason_data *data,
                    int count, void *user_data)
{
    (void)count;
    (void)user_data;

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN)
        return KaSON_ACTION_ENTER;

    if (data->event != KaSON_STREAM_EVENT_VALUE)
        return KaSON_CALLBACK_CONTINUE;

    if (key_is(key, "name") && data->type == KaSON_TYPE_STRING) {
        printf("name = %.*s\n", (int)(data->end - data->begin + 1),
               data->begin);
    } else if (key_is(key, "age") && data->type == KaSON_TYPE_NUMBER) {
        int age;
        if (kason_value_to_int(data->type, data->begin, data->end, &age) ==
                KaSON_CONVERT_SUCCESS)
            printf("age = %d\n", age);
    }

    return KaSON_CALLBACK_CONTINUE;
}

int main(void)
{
    char json[] = "{\"name\":\"Ada\",\"age\":36}";

    if (kason_parse(json, on_value, NULL) != KaSON_PARSE_RESULT_SUCCESS) {
        fputs("invalid JSON\n", stderr);
        return 1;
    }
    return 0;
}
```

Build and run it:

```sh
cc -std=c99 examples/regular.c kason.c -I. -o regular
./regular
```

Expected output:

```text
name = Ada
age = 36
```

Keys and values are inclusive pointer ranges into the original input. String
ranges do not include their quotes, but JSON escapes are still encoded. Use
`kason_strcmp()` and `kason_strcpy()` when strings may contain escapes. If the
input is not NUL-terminated, use `kason_parse_range(begin, end, ...)`; `end`
points to the last input byte, not one byte past it.

## 2. Stream API

The stream API is useful when the complete document is not available at once.
Initialize a `kason_stream`, feed each chunk in order, and always call
`kason_stream_finish()` after the final chunk.

```c
#include <stdio.h>
#include <string.h>

#include "kason.h"

static int on_fragment(kason_key *key, kason_stream_data *data,
                       void *user_data)
{
    (void)user_data;

    if (data->event == KaSON_STREAM_EVENT_VALUE) {
        if (key != NULL)
            printf("%.*s = ", (int)(key->end - key->begin + 1), key->begin);
        printf("%.*s\n", (int)(data->end - data->begin + 1), data->begin);
    }
    return KaSON_CALLBACK_CONTINUE;
}

int main(void)
{
    char first[] = "{\"name\":\"A";
    char second[] = "da\",\"age\":36}";
    char scratch[128];
    kason_stream stream;
    int result;

    result = kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                               on_fragment, NULL);
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == 0)
        result = kason_stream_feed(&stream, first, (int)strlen(first));
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == 0)
        result = kason_stream_feed(&stream, second, (int)strlen(second));
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == 0)
        result = kason_stream_finish(&stream);

    if (result != KaSON_PARSE_RESULT_SUCCESS) {
        fputs("invalid or incomplete JSON\n", stderr);
        return 1;
    }
    return 0;
}
```

Build it with the core library:

```sh
cc -std=c99 examples/stream.c kason.c -I. -o stream
./stream
```

The scratch buffer stores tokens that cross chunk boundaries. Increase it when
the input can contain longer keys, strings, numbers, or deeper nesting. Callback
pointers are temporary: consume or copy each key and fragment before the
callback returns. Primitive values arrive as complete `VALUE` events; nested
objects and arrays can arrive as `CONTAINER_BEGIN`, `CONTAINER_PART`, and
`CONTAINER_END` fragments.

`kason_stream_feed()` normally returns `KaSON_PARSE_RESULT_INCOMPLETE` while it
waits for more data. Stop feeding only when the result's major error class is
nonzero, then use the result from `kason_stream_finish()` as the final status.

Use `kason_stream_reset()` to parse another document with the same stream,
scratch buffer, callback, and user data.

## 3. Schema API

The schema API maps JSON object fields to a fixed C structure. First describe
the fields, initialize the schema, and then call `kason_unpack()`. The same
schema can also encode the structure with `kason_pack()`.

```c
#include <stdint.h>
#include <stdio.h>

#include "kason_schema.h"

typedef struct {
    char name[32];
    uint32_t age;
} person;

static const kason_schema_field person_fields[] = {
    KaSON_FIELD_STRING(person, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_U32(person, age, "age", KaSON_DEFAULT_U32(0))
};

KaSON_SCHEMA_DEFINE(person_schema, person, person_fields, 4);

int main(void)
{
    char json[] = "{\"name\":\"Ada\",\"age\":36}";
    char output[128];
    person value;
    kason_schema_error error;
    kason_writer writer;

    if (kason_schema_init(&person_schema) != KaSON_SCHEMA_SUCCESS) {
        fputs("invalid schema\n", stderr);
        return 1;
    }

    if (kason_unpack(json, &person_schema, &value, &error) !=
            KaSON_SCHEMA_SUCCESS) {
        fprintf(stderr, "unpack failed: %d\n", error.code);
        return 1;
    }
    printf("%s is %u\n", value.name, (unsigned)value.age);

    value.age++;
    if (kason_writer_init_buffer(&writer, output, sizeof(output)) !=
            KaSON_SCHEMA_SUCCESS ||
        kason_pack(&writer, &person_schema, &value, 0, &error) !=
            KaSON_SCHEMA_SUCCESS) {
        fprintf(stderr, "pack failed: %d\n", error.code);
        return 1;
    }

    puts(output);
    return 0;
}
```

Schema support requires both source files:

```sh
cc -std=c99 examples/schema.c kason.c kason_schema.c -I. -o schema
./schema
```

Expected output:

```text
Ada is 36
{"name":"Ada","age":37}
```

`KaSON_REQUIRED` rejects a document when the field is missing.
`KaSON_DEFAULT_U32(0)` supplies a default instead. Fixed-size strings and arrays
are checked before KaSON writes to them. For nested structures, define and
initialize every child schema before its parent schema.

## Quick choice

- Start with the regular API for a complete JSON document.
- Choose the stream API only when input genuinely arrives in chunks.
- Choose the schema API when the program already has a stable C data model.

For advanced topics such as capturing subtrees, selected-key parsing, writer
callbacks, and detailed ownership rules, see [the API guide](api-guide.md).
