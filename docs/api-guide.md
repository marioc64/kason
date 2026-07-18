# API guide {#api_guide}

This guide explains how the public KaSON interfaces fit together. The
complete symbol reference is generated from `kason.h` and `kason_schema.h`.

## Choosing an API

| Input or task | Recommended API |
| --- | --- |
| Complete NUL-terminated JSON | `kason_parse()` |
| Complete buffer with a known length | `kason_parse_range()` |
| Traverse a previously captured object or array | `kason_parse_container()` |
| Extract selected top-level object fields | `kason_parse_selected()` |
| JSON arriving in chunks | `kason_stream_init()`, `kason_stream_feed()`, `kason_stream_finish()` |
| Decode an object into a fixed C structure | `kason_unpack()` |
| Encode a fixed C structure | `kason_pack()` |

## Pointer ranges

Keys and values are represented by inclusive `begin` and `end` pointers. The
length of a nonempty slice is therefore `end - begin + 1`. Slices point into the
input buffer; they are not copied or NUL-terminated.

The application must keep the input alive and unchanged for as long as it uses
a saved slice. Conversion and decoding helpers accept the ranges directly, so
temporary copies normally are unnecessary.

## Parser events and actions

The action parser reports a container as soon as its opening `{` or `[` is
recognized. A `kason_parse_callback` chooses one of four actions:

- `KaSON_ACTION_ENTER` visits the container's immediate children and later
  reports `KaSON_STREAM_EVENT_CONTAINER_END`.
- `KaSON_ACTION_CAPTURE` validates the contents without visiting them and later
  reports the complete container range.
- `KaSON_ACTION_SKIP` validates the contents without child or end callbacks.
- `KaSON_ACTION_BREAK` stops parsing successfully.

Primitive values produce `KaSON_STREAM_EVENT_VALUE`. Return
`KaSON_CALLBACK_CONTINUE` to continue or `KaSON_CALLBACK_BREAK` to stop.

Actions are local to each container. Returning ENTER for the root does not
automatically enter nested objects; the callback decides again when each nested
container begins.

## Keys and values

For object members, `kason_key` identifies the key. Array elements and the root
value have a null key. String key and value slices exclude the surrounding
quotes but retain JSON escape sequences.

Use:

- `kason_strcmp()` to compare decoded JSON strings;
- `kason_strlen()` to determine decoded UTF-8 length;
- `kason_strcpy()`, `kason_strcpy_utf16()`, or `kason_strcpy_utf32()` to decode;
- `kason_value_to_int()`, `kason_value_to_int64()`,
  `kason_value_to_uint64()`, or `kason_value_to_double()` for numbers.

## Selected-key parsing

`kason_lookup_table` uses storage provided by the application. Initialize the
table, add decoded UTF-8 key names, then reuse it for any number of documents.
The capacity should exceed the number of keys to leave open slots for hash
collisions.

Selected parsing validates the entire document and calls `kason_lookup_callback`
only for matching members of the top-level object. It does not use the
ENTER/CAPTURE/SKIP action protocol.

## Chunked streaming

`kason_stream` is a separate fragment-oriented interface for incomplete input.
Primitive values are emitted complete. Containers are emitted as begin, part,
and end fragments. The callback must consume or copy each reported fragment
before returning.

The caller supplies scratch memory. Tokens completed inside one feed can point
directly into that chunk; tokens crossing chunk boundaries may point into
scratch. Neither location remains stable after parsing continues.

Call `kason_stream_finish()` after the last chunk so truncated input and trailing
syntax are detected. `kason_stream_reset()` reuses the same parser, scratch, and
callback for another document.

## Schema lifecycle

Define schemas with `KaSON_SCHEMA_DEFINE()`. Initialize every child schema before
its parent by calling `kason_schema_init()`. After successful initialization a
schema is immutable and may be shared by concurrent readers.

Unpacking writes into application-owned structures. Fixed-size string and array
members are checked before writing. Unknown object keys are validated and
ignored. Required, duplicate, type, range, and capacity failures are reported
through the return code and optional `kason_schema_error`.

Writers support three destinations:

- `kason_writer_init_buffer()` writes a NUL-terminated JSON document;
- `kason_writer_init_callback()` sends output chunks to an application callback;
- `kason_writer_init_counter()` calculates the required encoded size.

## Errors and early termination

Core parse functions return `KaSON_PARSE_RESULT_SUCCESS` on a valid document,
including a callback-requested break. `KaSON_PARSE_RESULT_INCOMPLETE` indicates
that more input was required. Other nonzero results indicate invalid input,
invalid arguments, or insufficient caller-owned storage.

Because BREAK is successful early termination, it should not be used to report
an application error. Store application status in `user_data` and inspect it
after parsing returns.

Schema functions use the separate `KaSON_SCHEMA_SUCCESS` and
`KaSON_SCHEMA_ERROR_*` result family.

## Thread safety

Parsing has no global mutable state. Separate parser/stream instances and input
buffers can be used concurrently. An initialized schema is read-only; writer,
output, lookup, and scratch storage must not be shared concurrently unless the
application provides synchronization.
