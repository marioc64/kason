#ifndef KaSON_H
#define KaSON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file kason.h
 * @brief Allocation-free JSON parsing and conversion API.
 *
 * All ranges are inclusive. Reported key and value slices refer to the
 * caller-owned input and are not NUL-terminated.
 */

/** @defgroup kason_results Results, types, events, and actions
 * @brief Constants shared by the core parsing and conversion APIs.
 */
/** @defgroup kason_core Core parsing
 * @brief Parse complete buffers and traverse captured containers.
 */
/** @defgroup kason_selected Selected-field parsing
 * @brief Prepare reusable key tables and extract matching top-level fields.
 */
/** @defgroup kason_streaming Chunked streaming
 * @brief Parse JSON incrementally with caller-owned state and scratch storage.
 */
/** @defgroup kason_values Value conversion
 * @brief Convert JSON number slices and use convenience traversal helpers.
 */
/** @defgroup kason_strings String decoding
 * @brief Measure, decode, and compare escaped JSON string slices.
 */

/** @addtogroup kason_results
 * @{
 */

/** @def KaSON_PARSE_RESULT_MAJOR_MASK
 * @brief Mask for a result's error class. */
/** @def KaSON_PARSE_RESULT_MINOR_MASK
 * @brief Mask for result detail bits. */
/** @def KaSON_PARSE_RESULT_SUCCESS
 * @brief Parsing completed successfully. */
/** @def KaSON_PARSE_RESULT_INCOMPLETE
 * @brief Input ended before the document completed. */
/** @def KaSON_PARSE_RESULT_ERROR
 * @brief Generic JSON syntax-error class. */
/** @def KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK
 * @brief A required callback was NULL. */
/** @def KaSON_PARSE_RESULT_ERROR_BUFFER_FULL
 * @brief Caller-owned scratch was insufficient. */
/** @def KaSON_CALLBACK_CONTINUE
 * @brief Continue after a value or end event. */
/** @def KaSON_CALLBACK_BREAK
 * @brief Stop successfully after a value or end event. */
/** @def KaSON_ACTION_ENTER
 * @brief Visit a container's children and receive its end event. */
/** @def KaSON_ACTION_BREAK
 * @brief Stop successfully at a container-begin event. */
/** @def KaSON_ACTION_CAPTURE
 * @brief Validate silently and report the complete container at its end. */
/** @def KaSON_ACTION_SKIP
 * @brief Validate a container without child or end callbacks. */
/** @def KaSON_STRING_RESULT_ERROR
 * @brief A string slice was malformed. */
/** @def KaSON_CONVERT_SUCCESS
 * @brief Value conversion succeeded. */
/** @def KaSON_CONVERT_ERROR
 * @brief The value slice was malformed. */
/** @def KaSON_CONVERT_RANGE
 * @brief The value is outside the destination range. */
/** @def KaSON_CONVERT_TYPE_ERROR
 * @brief The supplied KaSON_TYPE_* cannot be converted. */
/** @def KaSON_MAX_NESTING
 * @brief Maximum accepted object/array nesting; defaults to 128. */
/** @def KaSON_TYPE_UNKNOWN
 * @brief Unknown or unavailable JSON type. */
/** @def KaSON_TYPE_NULL
 * @brief JSON `null`. */
/** @def KaSON_TYPE_TRUE
 * @brief JSON `true`. */
/** @def KaSON_TYPE_FALSE
 * @brief JSON `false`. */
/** @def KaSON_TYPE_STRING
 * @brief JSON string; the slice excludes quotes and retains escapes. */
/** @def KaSON_TYPE_NUMBER
 * @brief JSON number. */
/** @def KaSON_TYPE_ARRAY
 * @brief JSON array. */
/** @def KaSON_TYPE_OBJECT
 * @brief JSON object. */
/** @def KaSON_STREAM_EVENT_VALUE
 * @brief Complete primitive-value event. */
/** @def KaSON_STREAM_EVENT_CONTAINER_BEGIN
 * @brief Object/array opening event. */
/** @def KaSON_STREAM_EVENT_CONTAINER_PART
 * @brief Middle fragment from the chunked parser. */
/** @def KaSON_STREAM_EVENT_CONTAINER_END
 * @brief Container end or final stream fragment. */
/** @} */

#define KaSON_PARSE_RESULT_MAJOR_MASK		  ( 0xff00 )
#define KaSON_PARSE_RESULT_MINOR_MASK		  ( 0x00ff )

#define KaSON_PARSE_RESULT_SUCCESS             ( 0x0000 )
#define KaSON_PARSE_RESULT_INCOMPLETE          ( 0x0001 )

#define KaSON_PARSE_RESULT_ERROR               ( 0x0100 )
#define KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK ( 0x0200 )
#define KaSON_PARSE_RESULT_ERROR_BUFFER_FULL   ( 0x0104 )

#define KaSON_CALLBACK_CONTINUE ( 0 )
#define KaSON_CALLBACK_BREAK    ( 1 )

/* Returned from a container-begin callback. */
#define KaSON_ACTION_ENTER   ( 0 )
#define KaSON_ACTION_BREAK   ( 1 )
#define KaSON_ACTION_CAPTURE ( 2 )
#define KaSON_ACTION_SKIP    ( 3 )

#define KaSON_STRING_RESULT_ERROR ( -1 )

#define KaSON_CONVERT_SUCCESS    (  0 )
#define KaSON_CONVERT_ERROR      ( -1 )
#define KaSON_CONVERT_RANGE      ( -2 )
#define KaSON_CONVERT_TYPE_ERROR ( -3 )

#ifndef KaSON_MAX_NESTING
#define KaSON_MAX_NESTING ( 128 )
#endif

#define KaSON_TYPE_UNKNOWN ( -1 )
#define KaSON_TYPE_NULL    (  0 )
#define KaSON_TYPE_TRUE    (  1 )
#define KaSON_TYPE_FALSE   (  2 )
#define KaSON_TYPE_STRING  (  3 )
#define KaSON_TYPE_NUMBER  (  4 )
#define KaSON_TYPE_ARRAY   (  5 )
#define KaSON_TYPE_OBJECT  (  6 )

/** Inclusive input slice identifying an object-member key.
 * @ingroup kason_core
 */
typedef struct s_kason_key {
	char* begin; /**< First byte inside the key quotes. */
	char* end;   /**< Last byte inside the key quotes. */
} kason_key;

/** Value or container event emitted by the action parser.
 * @ingroup kason_core
 */
typedef struct s_kason_data {
	int type;    /**< One of KaSON_TYPE_*. */
	char* begin; /**< First byte of the event slice. */
	char* end;   /**< Last byte of the event slice. */
	int event;   /**< One of KaSON_STREAM_EVENT_*. */
} kason_data;

/** Prepared decoded UTF-8 object key for selective parsing.
 * @ingroup kason_selected
 */
typedef struct s_kason_lookup_key {
	const char* value; /**< Caller-owned decoded UTF-8 key. */
	int length;        /**< Length of @ref value in bytes. */
	uint32_t hash;     /**< Opaque hash initialized by the lookup API. */
} kason_lookup_key;

/** Caller-owned open-addressing table for selected keys.
 * @ingroup kason_selected
 */
typedef struct s_kason_lookup_table {
	kason_lookup_key* slots; /**< Caller-provided entry array. */
	int capacity;           /**< Number of entries in @ref slots. */
	int count;              /**< Number of keys stored. */
} kason_lookup_table;

/** Action-parser callback.
 * @ingroup kason_core
 * @param key Member key, or NULL for a root value or array element.
 * @param data Transient event descriptor.
 * @param count One for a primitive, or child count for a completed container.
 * @param user_data Opaque pointer supplied by the application.
 * @return KaSON_ACTION_* for container begin; KaSON_CALLBACK_* otherwise.
 */
typedef int (*kason_parse_callback)(kason_key* key, kason_data* data, int count, void* user_data);

/** Selected-key callback.
 * @ingroup kason_selected
 * @param matched_key Lookup-table entry that matched.
 * @param data Complete top-level value.
 * @param count Primitive or immediate-container element count.
 * @param user_data Opaque application pointer.
 * @return KaSON_CALLBACK_CONTINUE or KaSON_CALLBACK_BREAK.
 */
typedef int (*kason_lookup_callback)(const kason_lookup_key* matched_key,
		kason_data* data, int count, void* user_data);

#define KaSON_STREAM_EVENT_VALUE           ( 0 )
#define KaSON_STREAM_EVENT_CONTAINER_BEGIN ( 1 )
#define KaSON_STREAM_EVENT_CONTAINER_PART  ( 2 )
#define KaSON_STREAM_EVENT_CONTAINER_END   ( 3 )

/** Fragment emitted by the chunked parser.
 * @ingroup kason_streaming
 */
typedef struct s_kason_stream_data {
	int type;    /**< One of KaSON_TYPE_*. */
	int event;   /**< One of KaSON_STREAM_EVENT_*. */
	char* begin; /**< First fragment byte. */
	char* end;   /**< Last fragment byte. */
} kason_stream_data;

/** Chunked-parser callback; all pointers expire when the callback returns.
 * @ingroup kason_streaming
 * @param key Object-member key when applicable, otherwise NULL.
 * @param data Transient fragment descriptor.
 * @param user_data Opaque application pointer.
 * @return KaSON_CALLBACK_CONTINUE or KaSON_CALLBACK_BREAK.
 */
typedef int (*kason_stream_callback)(kason_key* key, kason_stream_data* data, void* user_data);

/** Caller-owned stream state, public only to permit stack allocation.
 * Treat its fields as internal and initialize it with kason_stream_init().
 * @ingroup kason_streaming
 */
typedef struct s_kason_stream {
	/** @cond INTERNAL */
	char* scratch;
	int scratch_size;
	int scratch_length;
	kason_stream_callback callback;
	void* user_data;
	int mode;
	int state;
	int complete;
	int callback_break;
	int end_token;
	int key_begin;
	int key_end;
	int value_begin;
	int value_end;
	char* direct_key_begin;
	char* direct_key_end;
	char* direct_value_begin;
	char* direct_value_end;
	int has_key;
	int has_entries;
	int escape_hex_left;
	int container_type;
	int container_string;
	int container_escape;
	int container_hex_left;
	uint32_t container_unicode_value;
	uint32_t container_high_surrogate;
	uint32_t container_utf8_value;
	uint32_t container_utf8_minimum;
	int container_surrogate_state;
	int container_utf8_left;
	/** @endcond */
} kason_stream;

/** @brief Parse one NUL-terminated JSON document.
 * @ingroup kason_core
 * @param kason_buf Input buffer retained for all reported slices.
 * @param callback Event callback; must not be NULL.
 * @param user_data Opaque pointer passed to @p callback.
 * @return KaSON_PARSE_RESULT_SUCCESS, KaSON_PARSE_RESULT_INCOMPLETE, or an error result.
 */
int kason_parse(char* kason_buf, kason_parse_callback callback, void* user_data);

/** @brief Parse one JSON document from an inclusive byte range.
 * @ingroup kason_core
 * @param begin First input byte.
 * @param end Last input byte; must not precede @p begin.
 * @param callback Event callback; must not be NULL.
 * @param user_data Opaque pointer passed to @p callback.
 * @return KaSON_PARSE_RESULT_SUCCESS, KaSON_PARSE_RESULT_INCOMPLETE, or an error result.
 */
int kason_parse_range(char* begin, char* end, kason_parse_callback callback, void* user_data);

/** @brief Initialize one decoded UTF-8 lookup key.
 * @ingroup kason_selected
 * @param key Descriptor to initialize.
 * @param value Key bytes, retained by reference.
 * @param length Number of bytes in @p value.
 * @return KaSON_PARSE_RESULT_SUCCESS or an error result.
 */
int kason_lookup_key_init(kason_lookup_key* key, const char* value, int length);
/** @brief Initialize a lookup table over caller-owned slots.
 * @ingroup kason_selected
 * @param table Table to initialize.
 * @param slots Array containing @p capacity entries.
 * @param capacity Positive slot count.
 * @return KaSON_PARSE_RESULT_SUCCESS or an error result.
 */
int kason_lookup_table_init(kason_lookup_table* table, kason_lookup_key slots[],
		int capacity);
/** @brief Add one decoded UTF-8 key to an initialized table.
 * @ingroup kason_selected
 * @param table Table with at least one free slot.
 * @param value Key bytes, retained by reference.
 * @param length Number of key bytes.
 * @return KaSON_PARSE_RESULT_SUCCESS or an error result.
 */
int kason_lookup_table_add(kason_lookup_table* table, const char* value, int length);

/** @brief Validate a NUL-terminated document and report selected top-level fields.
 * @ingroup kason_selected
 * @param kason_buf NUL-terminated JSON object.
 * @param table Initialized lookup table.
 * @param callback Matching-field callback.
 * @param user_data Opaque callback pointer.
 * @return KaSON_PARSE_RESULT_SUCCESS, KaSON_PARSE_RESULT_INCOMPLETE, or an error result.
 */
int kason_parse_selected(char* kason_buf, const kason_lookup_table* table,
		kason_lookup_callback callback, void* user_data);
/** @brief Inclusive-range form of kason_parse_selected().
 * @ingroup kason_selected
 * @param begin First input byte.
 * @param end Last input byte, inclusive.
 * @param table Initialized lookup table.
 * @param callback Matching-field callback.
 * @param user_data Opaque callback pointer.
 * @return KaSON_PARSE_RESULT_SUCCESS, KaSON_PARSE_RESULT_INCOMPLETE, or an error result.
 */
int kason_parse_range_selected(char* begin, char* end,
		const kason_lookup_table* table,
		kason_lookup_callback callback, void* user_data);

/** @brief Parse immediate children of a captured object or array.
 * @ingroup kason_core
 * @param container Complete KaSON_TYPE_OBJECT or KaSON_TYPE_ARRAY slice.
 * @param callback Child event callback.
 * @param user_data Opaque callback pointer.
 * @return KaSON_PARSE_RESULT_SUCCESS or an error result.
 */
int kason_parse_container(kason_data* container, kason_parse_callback callback, void* user_data);

/** @brief Initialize a chunked parser.
 * @ingroup kason_streaming
 * Primitive values are emitted complete. Containers are emitted as begin,
 * part, and end fragments. Scratch stores tokens crossing feed boundaries,
 * escaped tokens requiring assembly, and nested-container state.
 * @param stream State object to initialize.
 * @param scratch Caller-owned scratch, or NULL when @p scratch_size is zero.
 * @param scratch_size Scratch capacity in bytes.
 * @param callback Fragment callback; must not be NULL.
 * @param user_data Opaque callback pointer.
 * @return KaSON_PARSE_RESULT_SUCCESS or an error result.
 */
int kason_stream_init(kason_stream* stream, char* scratch, int scratch_size, kason_stream_callback callback, void* user_data);

/** @brief Feed the next input chunk.
 * @ingroup kason_streaming
 * @param stream Initialized parser.
 * @param chunk Input bytes valid throughout this call.
 * @param length Number of bytes in @p chunk; zero is allowed.
 * @return KaSON_PARSE_RESULT_SUCCESS or an error result.
 */
int kason_stream_feed(kason_stream* stream, char* chunk, int length);

/** @brief Mark end of input and validate completeness.
 * @ingroup kason_streaming
 * @param stream Initialized parser.
 * @return KaSON_PARSE_RESULT_SUCCESS, KaSON_PARSE_RESULT_INCOMPLETE, or an error result.
 */
int kason_stream_finish(kason_stream* stream);

/** @brief Reset for another document while retaining scratch and callback.
 * @ingroup kason_streaming
 * @param stream Previously initialized parser.
 */
void kason_stream_reset(kason_stream* stream);

/** @brief Parse immediate array values into caller-owned descriptors.
 * @ingroup kason_values
 * @param begin Opening `[` byte.
 * @param end Closing `]` byte, inclusive.
 * @param array Destination descriptor array.
 * @param max_elements Capacity of @p array.
 * @return Stored element count, or a negative/error result on failure.
 */
int kason_parse_array(char* begin, char* end, kason_data array[], const int max_elements);

/** @brief Find an immediate object member by decoded key comparison.
 * @ingroup kason_values
 * @param begin Opening `{` byte.
 * @param end Closing `}` byte, inclusive.
 * @param for_key JSON-string slice containing the requested key.
 * @param out_value Receives the descriptor when found.
 * @return 1 when found, 0 when absent, or a negative/error result on failure.
 */
int kason_get_value(char* begin, char* end, kason_key* for_key, kason_data* out_value);

/** @brief Convert an integer-form JSON number to `int`.
 * @ingroup kason_values
 * @param type Must be KaSON_TYPE_NUMBER. @param begin First number byte.
 * @param end Last number byte. @param out_value Destination, unchanged on failure.
 * @return A KaSON_CONVERT_* result.
 */
int kason_value_to_int(int type, const char* begin, const char* end, int* out_value);
/** @brief Convert an integer-form JSON number to `int64_t`.
 * @ingroup kason_values
 * @param type Must be KaSON_TYPE_NUMBER. @param begin First number byte.
 * @param end Last number byte. @param out_value Destination, unchanged on failure.
 * @return A KaSON_CONVERT_* result.
 */
int kason_value_to_int64(int type, const char* begin, const char* end, int64_t* out_value);
/** @brief Convert a nonnegative integer-form JSON number to `uint64_t`.
 * @ingroup kason_values
 * @param type Must be KaSON_TYPE_NUMBER. @param begin First number byte.
 * @param end Last number byte. @param out_value Destination, unchanged on failure.
 * @return A KaSON_CONVERT_* result.
 */
int kason_value_to_uint64(int type, const char* begin, const char* end, uint64_t* out_value);
/** @brief Convert a JSON number to `double`.
 * @ingroup kason_values
 * @param type Must be KaSON_TYPE_NUMBER. @param begin First number byte.
 * @param end Last number byte. @param out_value Destination, unchanged on failure.
 * @return A KaSON_CONVERT_* result.
 */
int kason_value_to_double(int type, const char* begin, const char* end, double* out_value);

/** @brief Calculate decoded UTF-8 length.
 * @ingroup kason_strings
 * @param kason_string_begin First byte inside the quotes.
 * @param kason_string_end Last byte inside the quotes, inclusive.
 * @return Decoded byte count or KaSON_STRING_RESULT_ERROR.
 */
int kason_strlen(const char* kason_string_begin, const char* kason_string_end);

/** @brief Decode a JSON-string slice to UTF-8 without appending a terminator.
 * @ingroup kason_strings
 * @param kason_string_begin First byte inside the quotes.
 * @param kason_string_end Last byte inside the quotes, inclusive.
 * @param buffer Destination, or NULL to calculate required size.
 * @param buffer_size Destination capacity in bytes.
 * @return Required/decoded byte count or KaSON_STRING_RESULT_ERROR.
 */
int kason_strcpy(const char* kason_string_begin, const char* kason_string_end, char* buffer, int buffer_size);

/** @brief Decode a JSON-string slice to UTF-16 without appending a terminator.
 * @ingroup kason_strings
 * @param kason_string_begin First byte inside the quotes.
 * @param kason_string_end Last byte inside the quotes, inclusive.
 * @param buffer Destination, or NULL to calculate required size.
 * @param buffer_size Capacity in uint16_t code units.
 * @return Required/decoded unit count or KaSON_STRING_RESULT_ERROR.
 */
int kason_strcpy_utf16(const char* kason_string_begin, const char* kason_string_end, uint16_t* buffer, int buffer_size);

/** @brief Decode a JSON-string slice to UTF-32 without appending a terminator.
 * @ingroup kason_strings
 * @param kason_string_begin First byte inside the quotes.
 * @param kason_string_end Last byte inside the quotes, inclusive.
 * @param buffer Destination, or NULL to calculate required size.
 * @param buffer_size Capacity in uint32_t scalar values.
 * @return Required/decoded scalar count or KaSON_STRING_RESULT_ERROR.
 */
int kason_strcpy_utf32(const char* kason_string_begin, const char* kason_string_end, uint32_t* buffer, int buffer_size);

/** @brief Compare two JSON-string slices by decoded Unicode scalar value.
 * @ingroup kason_strings
 * @param kason_string1_begin First byte of the first slice.
 * @param kason_string1_end Last byte of the first slice.
 * @param kason_string2_begin First byte of the second slice.
 * @param kason_string2_end Last byte of the second slice.
 * @return A value less than, equal to, or greater than zero.
 */
int kason_strcmp(const char* kason_string1_begin, const char* kason_string1_end, const char* kason_string2_begin, const char* kason_string2_end);

#ifdef __cplusplus
}
#endif

#endif // KaSON_H
