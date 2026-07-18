# KaSON API tutorial

[Documentation home](index.md) · [API guide](api-guide.md) ·
[Compatibility](compatibility.md)

KaSON offers three primary workflows:

| API | Use it when |
| --- | --- |
| Regular | A complete JSON document is already in memory |
| Stream | JSON arrives in chunks, for example from a socket |
| Schema | JSON maps directly to or from a stable C structure |

All three avoid heap allocation. Input buffers, stream scratch space,
structures, and output buffers belong to the application. The programs under
[`examples/`](../examples) are the authoritative, compiled versions of the
excerpts below.

## Regular API

The regular API parses a complete NUL-terminated string with `kason_parse()`.
The callback returns `KaSON_ACTION_ENTER` when a container should be traversed,
then handles its values:

```c
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
```

Build and run the [complete example](../examples/regular.c):

```sh
cc -std=c99 examples/regular.c kason.c -I. -o regular
./regular
```

```text
name = Ada
age = 36
```

**Lifetime:** Keys and values are inclusive ranges into the original input.
They remain usable only while that buffer remains alive and unchanged. String
ranges omit their quotes but retain JSON escapes; use `kason_strcmp()` and
`kason_strcpy()` when strings may contain escapes.

For a buffer with a known length, use `kason_parse_range(begin, end, ...)`.
`end` points to the final input byte, not one byte past it.

## Stream API

Initialize a `kason_stream`, feed chunks in order, and always finish the stream
after the final chunk:

```c
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
```

Build and run the [complete example](../examples/stream.c):

```sh
cc -std=c99 examples/stream.c kason.c -I. -o stream
./stream
```

Although `"Ada"` is split across the chunks, the primitive arrives as one
complete value event:

```text
name = Ada
age = 36
```

**Lifetime:** Stream callback pointers expire when the callback returns. Consume
or copy each key and fragment immediately. The scratch buffer holds tokens that
cross chunk boundaries, escaped tokens requiring assembly, and nested-container
state.

`kason_stream_feed()` may return `KaSON_PARSE_RESULT_INCOMPLETE` while waiting
for more data. Stop early only when the major error class is nonzero, and use
`kason_stream_finish()` for the final status. Call `kason_stream_reset()` to
reuse the parser, scratch buffer, callback, and user data.

## Schema API

The schema API maps object fields to a fixed C structure. Declare the fields,
define storage, initialize the schema, and then unpack or pack:

```c
typedef struct {
    char name[32];
    uint32_t age;
} person;

static const kason_schema_field person_fields[] = {
    KaSON_FIELD_STRING(person, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_U32(person, age, "age", KaSON_DEFAULT_U32(0))
};

KaSON_SCHEMA_DEFINE(person_schema, person, person_fields, 4);

if (kason_schema_init(&person_schema) != KaSON_SCHEMA_SUCCESS)
    return 1;
if (kason_unpack(json, &person_schema, &value, &error) !=
        KaSON_SCHEMA_SUCCESS)
    return 1;
```

Build and run the [complete example](../examples/schema.c):

```sh
cc -std=c99 examples/schema.c kason.c kason_schema.c -I. -o schema
./schema
```

```text
Ada is 36
{"name":"Ada","age":37}
```

`KaSON_REQUIRED` rejects missing fields; a `KaSON_DEFAULT_*` policy supplies a
default instead. String and array capacities are derived from their fixed C
arrays and checked before writing. Define and initialize every child schema
before its parent. See [`examples/schema_config.c`](../examples/schema_config.c)
for nested structures and arrays.

## Where to go next

- Capturing or skipping subtrees: [parser events and actions](api-guide.md#parser-events-and-actions)
- Selected top-level fields: [selected-key parsing](api-guide.md#selected-key-parsing)
- Writer callbacks and exact sizing: [writer modes](api-guide.md#writer-modes)
- Ownership rules: [pointer and storage lifetimes](api-guide.md#pointer-and-storage-lifetimes)
- Return codes: [errors and early termination](api-guide.md#errors-and-early-termination)
