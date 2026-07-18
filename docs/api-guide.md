# API guide

[Documentation home](index.md) · [Tutorial](tutorial.md) ·
[Compatibility](compatibility.md)

This guide describes how the public KaSON interfaces fit together. The
generated symbol reference contains exact declarations from `kason.h` and
`kason_schema.h`.

## Choosing an API

| Input or task | Recommended API |
| --- | --- |
| Complete NUL-terminated JSON | `kason_parse()` |
| Complete buffer with a known length | `kason_parse_range()` |
| Traverse a captured object or array | `kason_parse_container()` |
| Extract selected top-level fields | `kason_parse_selected()` |
| JSON arriving in chunks | `kason_stream_init()`, `kason_stream_feed()`, `kason_stream_finish()` |
| Decode an object into a fixed C structure | `kason_unpack()` |
| Encode a fixed C structure | `kason_pack()` |

## Pointer and storage lifetimes

| API or object | Storage owner | Validity |
| --- | --- | --- |
| Regular-parser keys and values | Application input buffer | Until the input is changed or released |
| Captured container | Application input buffer | Same lifetime as its input |
| Lookup-key text and slots | Application | For every parse using the table |
| Stream callback pointers | Input chunk or stream scratch | Only during the callback |
| Schema object and lookup storage | Application or static storage | After initialization and throughout use |
| Writer buffer or scratch | Application | Throughout the writer operation |

All pointer ranges are inclusive: a nonempty slice length is
`end - begin + 1`. Slices are not copied or NUL-terminated.

## Parser events and actions

The action parser reports a container when its opening `{` or `[` is recognized.
The callback response depends on the event:

| Event | Valid callback results |
| --- | --- |
| `KaSON_STREAM_EVENT_CONTAINER_BEGIN` | `KaSON_ACTION_ENTER`, `KaSON_ACTION_CAPTURE`, `KaSON_ACTION_SKIP`, `KaSON_ACTION_BREAK` |
| `KaSON_STREAM_EVENT_VALUE` | `KaSON_CALLBACK_CONTINUE`, `KaSON_CALLBACK_BREAK` |
| `KaSON_STREAM_EVENT_CONTAINER_END` | `KaSON_CALLBACK_CONTINUE`, `KaSON_CALLBACK_BREAK` |

ENTER visits immediate children and later reports the container end. CAPTURE
validates children silently and reports the complete container range at its end.
SKIP validates without child or end callbacks. BREAK stops successfully.

Actions are local: entering the root does not automatically enter nested
containers. A callback chooses again for every nested object or array.

## Keys, values, and conversions

Object members have a `kason_key`; array elements and the root value have a null
key. String slices exclude quotes but retain JSON escape sequences.

| Task | API |
| --- | --- |
| Compare decoded strings | `kason_strcmp()` |
| Calculate decoded UTF-8 length | `kason_strlen()` |
| Decode UTF-8, UTF-16, or UTF-32 | `kason_strcpy()`, `kason_strcpy_utf16()`, `kason_strcpy_utf32()` |
| Convert signed integers | `kason_value_to_int()`, `kason_value_to_int64()` |
| Convert an unsigned integer | `kason_value_to_uint64()` |
| Convert floating point | `kason_value_to_double()` |

Integer conversions reject fractions and exponents. Conversion destinations
remain unchanged on failure. String copy functions do not append a terminator;
reserve and append one when a C string is required.

## Selected-key parsing

`kason_lookup_table` uses caller-provided slots. Initialize the table, add
decoded UTF-8 key names, and reuse it across documents. Capacity must be greater
than the number of keys so open-addressing can always find a free slot.

`kason_lookup_key_init()` prepares a standalone descriptor. Most applications
can use `kason_lookup_table_add()`, which performs that initialization and adds
the key to the table.

Selected parsing validates the complete document and invokes
`kason_lookup_callback` only for matching top-level members. It does not use the
ENTER/CAPTURE/SKIP protocol. Use `kason_parse_range_selected()` for a bounded
buffer.

## Chunked streaming

The stream API is fragment-oriented. Primitive values are emitted complete;
containers can produce begin, part, and end fragments. Tokens completed within
one feed may point into that chunk, while tokens crossing feeds may use scratch.
Neither location remains stable after the callback returns.

Scratch capacity must cover keys or primitive tokens crossing a chunk boundary,
escaped tokens requiring assembly, and nested-container tracking. A buffer-full
result means the application must retry the document with more scratch.

Always call `kason_stream_finish()` after the final chunk to detect truncated
input or trailing syntax. `kason_stream_reset()` reuses the same configuration
for another document.

## Convenience traversal helpers

`kason_parse_array()` collects immediate values from an array range into a
caller-owned descriptor array. It returns the number stored and validates the
input; when capacity is smaller than the array, only that many descriptors are
stored.

`kason_get_value()` searches an immediate object for a decoded key. It returns
1 when found, 0 when absent, and a negative/error result for invalid arguments
or input. The returned value remains a view into the input buffer.

Use the action parser or selected-key parser for repeated or more complex
traversal.

## Schema lifecycle

Define schemas with `KaSON_SCHEMA_DEFINE()`, then initialize every child before
its parent with `kason_schema_init()`. After successful initialization, schemas
are immutable and may be shared by concurrent readers.

Schema field macros require fixed-layout members:

| Macro family | Required member type |
| --- | --- |
| `KaSON_FIELD_BOOL`, `KaSON_FIELD_INT` | `int` |
| `KaSON_FIELD_INT64` | `int64_t` |
| `KaSON_FIELD_U32` | `uint32_t` |
| `KaSON_FIELD_U64` | `uint64_t` |
| `KaSON_FIELD_DOUBLE` | `double` |
| `KaSON_FIELD_STRING` | Fixed `char` array |
| `KaSON_FIELD_STRING_U16` | Fixed `uint16_t` array |
| `KaSON_FIELD_STRING_U32` | Fixed `uint32_t` array |
| `KaSON_FIELD_STRUCT` | Fixed-layout structure described by the child schema |

The corresponding array macro requires a fixed array of the same element type
and a `size_t` count member.

- String capacities are derived from fixed `char`, `uint16_t`, or `uint32_t`
  arrays and include space for the zero terminator.
- `KaSON_DEFAULT_STRING*` arguments must be arrays or literals because their
  size is derived with `sizeof`. String and structure defaults are retained by
  reference and must outlive all schema operations.
- Nested struct fields require an initialized child schema.

Unpacking initializes the destination from defaults before parsing. Unknown
keys are validated and ignored. Required, duplicate, type, numeric-range, and
capacity failures are returned and described by the optional
`kason_schema_error`.

The range functions accept an inclusive object range. The flagged forms accept
`KaSON_UNPACK_RELAXED`, which stops once every schema field has been decoded.
That mode deliberately does not validate remaining syntax or later duplicate
fields and should only be used when this trade-off is acceptable.

## Writer modes

| Initializer | Destination | Notes |
| --- | --- | --- |
| `kason_writer_init_buffer()` | Fixed buffer | Produces a NUL-terminated document; capacity includes the terminator |
| `kason_writer_init_callback()` | Callback | Uses optional staging scratch; callback output may be partial on failure |
| `kason_writer_init_counter()` | Counter only | Calculates the exact encoded byte length without producing output |

`kason_writer_length()` excludes the buffer-mode terminator. A common exact-size
workflow is to pack once with a counter, allocate or select a buffer of
`length + 1`, then pack with a buffer writer. `kason_writer_reset()` clears
length and status while retaining the writer configuration.

`KaSON_PACK_OMIT_DEFAULTS` omits fields equal to their declared defaults.

## Errors and early termination

| Result family | Meaning |
| --- | --- |
| `KaSON_PARSE_RESULT_SUCCESS` | Valid document or successful callback-requested break |
| `KaSON_PARSE_RESULT_INCOMPLETE` | More input was required |
| `KaSON_PARSE_RESULT_ERROR*` | Syntax, argument, or scratch-capacity failure |
| `KaSON_CONVERT_SUCCESS` | Successful scalar conversion |
| `KaSON_CONVERT_ERROR`, `KaSON_CONVERT_RANGE`, `KaSON_CONVERT_TYPE_ERROR` | Malformed, out-of-range, or incompatible conversion |
| `KaSON_SCHEMA_SUCCESS` | Successful schema or writer operation |
| `KaSON_SCHEMA_ERROR_*` | Schema definition, input, capacity, or writer failure |

Use `KaSON_PARSE_RESULT_MAJOR_MASK` to distinguish a stream's error class from
the nonfatal incomplete state. Because BREAK is successful early termination,
do not use it to signal an application failure; record application status in
`user_data` and inspect it after parsing returns.

## Thread safety

Parsing has no global mutable state. Separate parser or stream instances and
input buffers can be used concurrently. An initialized schema is read-only.
Writer, output, lookup, and scratch storage require application synchronization
if shared.
