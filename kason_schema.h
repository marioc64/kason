#ifndef KaSON_SCHEMA_H
#define KaSON_SCHEMA_H

#include <stddef.h>
#include <stdint.h>

#include "kason.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @file kason_schema.h
 * @brief Fixed-layout JSON-to-C schema and JSON writer API.
 *
 * Schema definitions, lookup storage, decoded objects, and writer storage are
 * all caller-owned. Initialize child schemas before their parents.
 */

/** @defgroup kason_schema_results Schema results and types
 * @brief Result codes, field kinds, flags, and error details.
 */
/** @defgroup kason_schema_definition Schema definition
 * @brief Macros and structures that map JSON fields to fixed-layout C objects.
 *
 * String members are fixed arrays of char, uint16_t, or uint32_t and their
 * capacity includes the zero terminator. Array fields use fixed member arrays
 * plus a size_t count member. Key and string-default arguments must be literals
 * or arrays because the macros derive their sizes with sizeof. String and
 * structure defaults are retained by reference and must outlive every schema
 * operation. Nested structure fields reference a child schema initialized
 * before its parent.
 *
 * @code
 * static const kason_schema_field fields[] = {
 *     KaSON_FIELD_STRING(config, name, "name", KaSON_REQUIRED),
 *     KaSON_FIELD_U32(config, port, "port", KaSON_DEFAULT_U32(1883))
 * };
 * KaSON_SCHEMA_DEFINE(config_schema, config, fields, 4);
 * @endcode
 */
/** @defgroup kason_schema_unpack Schema unpacking
 * @brief Initialize schemas and decode JSON objects into C structures.
 */
/** @defgroup kason_writers JSON writers
 * @brief Encode schema-backed structures to buffers, callbacks, or counters.
 */

/** @addtogroup kason_schema_results
 * @{
 */

/** @def KaSON_SCHEMA_MAX_FIELDS
 * @brief Maximum number of fields in one schema. */
/** @def KaSON_SCHEMA_SUCCESS
 * @brief Schema operation succeeded. */
/** @def KaSON_SCHEMA_ERROR
 * @brief Generic schema error. */
/** @def KaSON_SCHEMA_ERROR_INVALID_SCHEMA
 * @brief Schema definition or argument is invalid. */
/** @def KaSON_SCHEMA_ERROR_MISSING_FIELD
 * @brief A required field was absent. */
/** @def KaSON_SCHEMA_ERROR_DUPLICATE_FIELD
 * @brief A JSON field occurred more than once. */
/** @def KaSON_SCHEMA_ERROR_TYPE
 * @brief JSON type does not match the field type. */
/** @def KaSON_SCHEMA_ERROR_RANGE
 * @brief Numeric value cannot fit the destination. */
/** @def KaSON_SCHEMA_ERROR_STRING_CAPACITY
 * @brief Decoded string exceeds its fixed array. */
/** @def KaSON_SCHEMA_ERROR_ARRAY_CAPACITY
 * @brief JSON array exceeds its fixed array. */
/** @def KaSON_SCHEMA_ERROR_WRITER_CAPACITY
 * @brief Writer buffer or scratch is insufficient. */
/** @def KaSON_SCHEMA_ERROR_WRITER_CALLBACK
 * @brief Output callback rejected a chunk. */
/** @def KaSON_SCHEMA_ERROR_NESTING
 * @brief Schema nesting limit was exceeded. */
/** @def KaSON_SCHEMA_TYPE_BOOL
 * @brief C boolean-like integer field. */
/** @def KaSON_SCHEMA_TYPE_INT
 * @brief C `int` field. */
/** @def KaSON_SCHEMA_TYPE_INT64
 * @brief C `int64_t` field. */
/** @def KaSON_SCHEMA_TYPE_U32
 * @brief C `uint32_t` field. */
/** @def KaSON_SCHEMA_TYPE_U64
 * @brief C `uint64_t` field. */
/** @def KaSON_SCHEMA_TYPE_DOUBLE
 * @brief C `double` field. */
/** @def KaSON_SCHEMA_TYPE_STRING
 * @brief NUL-terminated UTF-8 `char` array. */
/** @def KaSON_SCHEMA_TYPE_STRUCT
 * @brief Nested structure field. */
/** @def KaSON_SCHEMA_TYPE_STRING_U16
 * @brief Zero-terminated UTF-16 array. */
/** @def KaSON_SCHEMA_TYPE_STRING_U32
 * @brief Zero-terminated UTF-32 array. */
/** @def KaSON_SCHEMA_FIELD_ARRAY
 * @brief Marks a fixed-capacity array field. */
/** @def KaSON_SCHEMA_POLICY_REQUIRED
 * @brief Field must occur in the input. */
/** @def KaSON_SCHEMA_POLICY_DEFAULT
 * @brief Apply a default when the field is absent. */
/** @def KaSON_PACK_OMIT_DEFAULTS
 * @brief Do not encode fields equal to their defaults. */
/** @def KaSON_UNPACK_RELAXED
 * @brief Stop after every schema field has been decoded. */
/** @def KaSON_SCHEMA_NO_OFFSET
 * @brief Sentinel for an unused structure offset. */
/** @def KaSON_SCHEMA_NO_FIELD
 * @brief Sentinel for an absent field index. */
/** @def KaSON_WRITER_MODE_BUFFER
 * @brief Writer targets a fixed buffer. */
/** @def KaSON_WRITER_MODE_CALLBACK
 * @brief Writer targets an output callback. */
/** @def KaSON_WRITER_MODE_COUNTER
 * @brief Writer only counts encoded bytes. */
/** @} */

/** @addtogroup kason_schema_definition
 * @{
 */
/** @def KaSON_REQUIRED
 * @brief Policy requiring a field. */
/** @def KaSON_DEFAULT_BOOL
 * @brief Boolean default policy. */
/** @def KaSON_DEFAULT_INT
 * @brief `int` default policy. */
/** @def KaSON_DEFAULT_INT64
 * @brief `int64_t` default policy. */
/** @def KaSON_DEFAULT_U32
 * @brief `uint32_t` default policy. */
/** @def KaSON_DEFAULT_U64
 * @brief `uint64_t` default policy. */
/** @def KaSON_DEFAULT_DOUBLE
 * @brief `double` default policy. */
/** @def KaSON_DEFAULT_STRING
 * @brief UTF-8 default retained by reference; argument must be an array or literal. */
/** @def KaSON_DEFAULT_STRING_U16
 * @brief UTF-16 default retained by reference; argument must be an array. */
/** @def KaSON_DEFAULT_STRING_U32
 * @brief UTF-32 default retained by reference; argument must be an array. */
/** @def KaSON_DEFAULT_STRUCT
 * @brief Nested-structure default retained by reference. */
/** @def KaSON_DEFAULT_EMPTY_ARRAY
 * @brief Empty-array default policy. */
/** @def KaSON_FIELD_BOOL
 * @brief Declare a scalar boolean field. */
/** @def KaSON_FIELD_INT
 * @brief Declare a scalar `int` field. */
/** @def KaSON_FIELD_INT64
 * @brief Declare a scalar `int64_t` field. */
/** @def KaSON_FIELD_U32
 * @brief Declare a scalar `uint32_t` field. */
/** @def KaSON_FIELD_U64
 * @brief Declare a scalar `uint64_t` field. */
/** @def KaSON_FIELD_DOUBLE
 * @brief Declare a scalar `double` field. */
/** @def KaSON_FIELD_STRING
 * @brief Declare a fixed UTF-8 string field. */
/** @def KaSON_FIELD_STRING_U16
 * @brief Declare a fixed UTF-16 string field. */
/** @def KaSON_FIELD_STRING_U32
 * @brief Declare a fixed UTF-32 string field. */
/** @def KaSON_FIELD_STRUCT
 * @brief Declare a nested-structure field. */
/** @def KaSON_FIELD_BOOL_ARRAY
 * @brief Declare a fixed boolean array with a `size_t` count member. */
/** @def KaSON_FIELD_INT_ARRAY
 * @brief Declare a fixed `int` array with a `size_t` count member. */
/** @def KaSON_FIELD_INT64_ARRAY
 * @brief Declare a fixed `int64_t` array with a `size_t` count member. */
/** @def KaSON_FIELD_U32_ARRAY
 * @brief Declare a fixed `uint32_t` array with a `size_t` count member. */
/** @def KaSON_FIELD_U64_ARRAY
 * @brief Declare a fixed `uint64_t` array with a `size_t` count member. */
/** @def KaSON_FIELD_DOUBLE_ARRAY
 * @brief Declare a fixed `double` array with a `size_t` count member. */
/** @def KaSON_FIELD_STRING_ARRAY
 * @brief Declare fixed UTF-8 string arrays with a `size_t` count member. */
/** @def KaSON_FIELD_STRING_U16_ARRAY
 * @brief Declare fixed UTF-16 string arrays with a `size_t` count member. */
/** @def KaSON_FIELD_STRING_U32_ARRAY
 * @brief Declare fixed UTF-32 string arrays with a `size_t` count member. */
/** @def KaSON_FIELD_STRUCT_ARRAY
 * @brief Declare fixed nested-structure arrays with a `size_t` count member. */
/** @def KaSON_SCHEMA_DEFINE
 * @brief Define schema storage and a mutable kason_schema object. */
/** @} */

/** @internal Implementation macro used by public policy macros. */
/** @def KaSON_SCHEMA_POLICY_INITIALIZER_ */
/** @internal Implementation macro used by public policy macros. */
/** @def KaSON_SCHEMA_POLICY_INITIALIZER */
/** @internal Computes a member size for schema declarations. */
/** @def KaSON_SCHEMA_MEMBER_SIZE */
/** @internal Computes a member offset for schema declarations. */
/** @def KaSON_SCHEMA_MEMBER_OFFSET */
/** @internal Implements scalar field macros. */
/** @def KaSON_SCHEMA_SCALAR_FIELD */
/** @internal Implements array field macros. */
/** @def KaSON_SCHEMA_ARRAY_FIELD */
/** @internal Implements KaSON_SCHEMA_DEFINE. */
/** @def KaSON_SCHEMA_INITIALIZER */

#define KaSON_SCHEMA_MAX_FIELDS ( 64 )

#define KaSON_SCHEMA_SUCCESS                 (  0 )
#define KaSON_SCHEMA_ERROR                   ( -1 )
#define KaSON_SCHEMA_ERROR_INVALID_SCHEMA    ( -2 )
#define KaSON_SCHEMA_ERROR_MISSING_FIELD     ( -3 )
#define KaSON_SCHEMA_ERROR_DUPLICATE_FIELD   ( -4 )
#define KaSON_SCHEMA_ERROR_TYPE              ( -5 )
#define KaSON_SCHEMA_ERROR_RANGE             ( -6 )
#define KaSON_SCHEMA_ERROR_STRING_CAPACITY   ( -7 )
#define KaSON_SCHEMA_ERROR_ARRAY_CAPACITY    ( -8 )
#define KaSON_SCHEMA_ERROR_WRITER_CAPACITY   ( -9 )
#define KaSON_SCHEMA_ERROR_WRITER_CALLBACK   (-10 )
#define KaSON_SCHEMA_ERROR_NESTING           (-11 )

#define KaSON_SCHEMA_TYPE_BOOL    ( 1 )
#define KaSON_SCHEMA_TYPE_INT     ( 2 )
#define KaSON_SCHEMA_TYPE_INT64   ( 3 )
#define KaSON_SCHEMA_TYPE_U32     ( 4 )
#define KaSON_SCHEMA_TYPE_U64     ( 5 )
#define KaSON_SCHEMA_TYPE_DOUBLE  ( 6 )
#define KaSON_SCHEMA_TYPE_STRING  ( 7 )
#define KaSON_SCHEMA_TYPE_STRUCT  ( 8 )
#define KaSON_SCHEMA_TYPE_STRING_U16 ( 9 )
#define KaSON_SCHEMA_TYPE_STRING_U32 (10 )

#define KaSON_SCHEMA_FIELD_ARRAY       ( 1u << 0 )
#define KaSON_SCHEMA_POLICY_REQUIRED   ( 1u << 0 )
#define KaSON_SCHEMA_POLICY_DEFAULT    ( 1u << 1 )

#define KaSON_PACK_OMIT_DEFAULTS       ( 1u << 0 )

/* Stop once every schema field has been decoded. Remaining input is ignored. */
#define KaSON_UNPACK_RELAXED           ( 1u << 0 )

#define KaSON_SCHEMA_NO_OFFSET ((size_t)-1)
#define KaSON_SCHEMA_NO_FIELD  UINT16_MAX

struct s_kason_schema;

/** Required/default policy stored in a field descriptor.
 * @ingroup kason_schema_definition
 */
typedef struct s_kason_schema_policy {
	unsigned flags;          /**< KaSON_SCHEMA_POLICY_* bit set. */
	int64_t signed_value;    /**< Signed-integer default storage. */
	uint64_t unsigned_value; /**< Unsigned-integer default storage. */
	double real_value;       /**< Floating-point default storage. */
	const void* pointer_value; /**< String/structure default source. */
	size_t value_size;       /**< Size of @ref pointer_value data. */
} kason_schema_policy;

/** Mapping between one JSON key and one C structure member.
 * @ingroup kason_schema_definition
 */
typedef struct s_kason_schema_field {
	const char* json_key; /**< Decoded UTF-8 key, normally a string literal. */
	int key_length;       /**< Key length in bytes. */
	size_t offset;        /**< Member offset in the containing structure. */
	size_t size;          /**< Total member size in bytes. */
	int type;             /**< One of KaSON_SCHEMA_TYPE_*. */
	unsigned flags;       /**< KaSON_SCHEMA_FIELD_* flags. */
	kason_schema_policy policy; /**< Required/default behavior. */
	const struct s_kason_schema* child_schema; /**< Schema for nested structures. */
	size_t count_offset;  /**< Array count-member offset or KaSON_SCHEMA_NO_OFFSET. */
	size_t count_size;    /**< Size of the array count member. */
	size_t element_size;  /**< Size of one array element. */
	size_t capacity;      /**< Scalar count (1) or fixed array capacity. */
} kason_schema_field;

/** Initialized lookup and layout description for one C structure.
 * @ingroup kason_schema_definition
 */
typedef struct s_kason_schema {
	const kason_schema_field* fields; /**< Field descriptor array. */
	int field_count;                 /**< Number of descriptors. */
	size_t object_size;              /**< Size of the mapped C structure. */
	kason_lookup_key* lookup_slots;    /**< Caller/static lookup storage. */
	uint16_t* slot_field_indices;     /**< Lookup-slot to field mapping. */
	int lookup_capacity;             /**< Number of lookup slots. */
	kason_lookup_table lookup;         /**< Prepared selected-key table. */
	uint64_t required_mask;           /**< Internal bit mask of required fields. */
	int initialized;                 /**< Nonzero after successful initialization. */
} kason_schema;

/** Detailed schema failure location.
 * @ingroup kason_schema_results
 */
typedef struct s_kason_schema_error {
	int code;                       /**< KaSON_SCHEMA_ERROR_* value. */
	const kason_schema_field* field; /**< Related field, or NULL. */
	int array_index;                /**< Related array index, or a negative value. */
	int depth;                      /**< Nested-schema depth at failure. */
} kason_schema_error;

/** Receives one encoded JSON chunk.
 * @ingroup kason_writers
 * @param data Output bytes; valid only during the callback.
 * @param length Number of bytes in @p data.
 * @param user_data Opaque application pointer.
 * @return Zero after consuming the chunk; nonzero to abort packing.
 */
typedef int (*kason_writer_callback)(const char* data, size_t length,
		void* user_data);

#define KaSON_WRITER_MODE_BUFFER   ( 1 )
#define KaSON_WRITER_MODE_CALLBACK ( 2 )
#define KaSON_WRITER_MODE_COUNTER  ( 3 )

/** Caller-owned JSON writer state. Initialize it with a writer initializer.
 * @ingroup kason_writers
 */
typedef struct s_kason_writer {
	int mode;                     /**< One of KaSON_WRITER_MODE_*. */
	char* buffer;                 /**< Fixed output buffer in buffer mode. */
	size_t capacity;              /**< Buffer capacity including final NUL. */
	size_t length;                /**< Encoded length excluding final NUL. */
	char* scratch;                /**< Callback-mode staging buffer. */
	size_t scratch_capacity;      /**< Staging-buffer capacity. */
	size_t scratch_length;        /**< Bytes currently staged. */
	kason_writer_callback callback; /**< Callback-mode destination. */
	void* user_data;              /**< Opaque callback pointer. */
	int status;                   /**< Current KaSON_SCHEMA_* status. */
} kason_writer;

#define KaSON_SCHEMA_POLICY_INITIALIZER_(flags_, signed_, unsigned_, real_, pointer_, size_) \
	{ (flags_), (signed_), (unsigned_), (real_), (pointer_), (size_) }
#define KaSON_SCHEMA_POLICY_INITIALIZER(policy_) \
	KaSON_SCHEMA_POLICY_INITIALIZER_ policy_

#define KaSON_REQUIRED \
	(KaSON_SCHEMA_POLICY_REQUIRED, 0, 0, 0.0, NULL, 0)
#define KaSON_DEFAULT_BOOL(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, (value_) ? 1 : 0, 0, 0.0, NULL, 0)
#define KaSON_DEFAULT_INT(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, (int64_t)(value_), 0, 0.0, NULL, 0)
#define KaSON_DEFAULT_INT64(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, (int64_t)(value_), 0, 0.0, NULL, 0)
#define KaSON_DEFAULT_U32(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, (uint64_t)(value_), 0.0, NULL, 0)
#define KaSON_DEFAULT_U64(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, (uint64_t)(value_), 0.0, NULL, 0)
#define KaSON_DEFAULT_DOUBLE(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, 0, (double)(value_), NULL, 0)
#define KaSON_DEFAULT_STRING(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, 0, 0.0, (value_), sizeof(value_) - 1)
#define KaSON_DEFAULT_STRING_U16(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, 0, 0.0, (value_), \
	 sizeof(value_) / sizeof((value_)[0]) - 1)
#define KaSON_DEFAULT_STRING_U32(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, 0, 0.0, (value_), \
	 sizeof(value_) / sizeof((value_)[0]) - 1)
#define KaSON_DEFAULT_STRUCT(value_) \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, 0, 0.0, &(value_), sizeof(value_))
#define KaSON_DEFAULT_EMPTY_ARRAY \
	(KaSON_SCHEMA_POLICY_DEFAULT, 0, 0, 0.0, NULL, 0)

#define KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_) \
	sizeof(((struct_type_*)0)->member_)
#define KaSON_SCHEMA_MEMBER_OFFSET(struct_type_, member_) \
	offsetof(struct_type_, member_)

#define KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, type_, policy_) \
	{ (key_), (int)(sizeof(key_) - 1), \
	  KaSON_SCHEMA_MEMBER_OFFSET(struct_type_, member_), \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_), \
	  (type_), 0, KaSON_SCHEMA_POLICY_INITIALIZER(policy_), NULL, \
	  KaSON_SCHEMA_NO_OFFSET, 0, \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_), 1 }

#define KaSON_FIELD_BOOL(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_BOOL, policy_)
#define KaSON_FIELD_INT(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_INT, policy_)
#define KaSON_FIELD_INT64(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_INT64, policy_)
#define KaSON_FIELD_U32(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_U32, policy_)
#define KaSON_FIELD_U64(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_U64, policy_)
#define KaSON_FIELD_DOUBLE(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_DOUBLE, policy_)
#define KaSON_FIELD_STRING(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_STRING, policy_)
#define KaSON_FIELD_STRING_U16(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_STRING_U16, policy_)
#define KaSON_FIELD_STRING_U32(struct_type_, member_, key_, policy_) \
	KaSON_SCHEMA_SCALAR_FIELD(struct_type_, member_, key_, \
		KaSON_SCHEMA_TYPE_STRING_U32, policy_)

#define KaSON_FIELD_STRUCT(struct_type_, member_, key_, child_, policy_) \
	{ (key_), (int)(sizeof(key_) - 1), \
	  KaSON_SCHEMA_MEMBER_OFFSET(struct_type_, member_), \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_), \
	  KaSON_SCHEMA_TYPE_STRUCT, 0, KaSON_SCHEMA_POLICY_INITIALIZER(policy_), \
	  (child_), \
	  KaSON_SCHEMA_NO_OFFSET, 0, \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_), 1 }

#define KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_member_, key_, \
		type_, child_, policy_) \
	{ (key_), (int)(sizeof(key_) - 1), \
	  KaSON_SCHEMA_MEMBER_OFFSET(struct_type_, member_), \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_), \
	  (type_), KaSON_SCHEMA_FIELD_ARRAY, KaSON_SCHEMA_POLICY_INITIALIZER(policy_), \
	  (child_), \
	  KaSON_SCHEMA_MEMBER_OFFSET(struct_type_, count_member_), \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, count_member_), \
	  sizeof(((struct_type_*)0)->member_[0]), \
	  KaSON_SCHEMA_MEMBER_SIZE(struct_type_, member_) / \
		sizeof(((struct_type_*)0)->member_[0]) }

#define KaSON_FIELD_BOOL_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_BOOL, NULL, policy_)
#define KaSON_FIELD_INT_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_INT, NULL, policy_)
#define KaSON_FIELD_INT64_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_INT64, NULL, policy_)
#define KaSON_FIELD_U32_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_U32, NULL, policy_)
#define KaSON_FIELD_U64_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_U64, NULL, policy_)
#define KaSON_FIELD_DOUBLE_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_DOUBLE, NULL, policy_)
#define KaSON_FIELD_STRING_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_STRING, NULL, policy_)
#define KaSON_FIELD_STRING_U16_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_STRING_U16, NULL, policy_)
#define KaSON_FIELD_STRING_U32_ARRAY(struct_type_, member_, count_, key_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_STRING_U32, NULL, policy_)
#define KaSON_FIELD_STRUCT_ARRAY(struct_type_, member_, count_, key_, child_, policy_) \
	KaSON_SCHEMA_ARRAY_FIELD(struct_type_, member_, count_, key_, \
		KaSON_SCHEMA_TYPE_STRUCT, child_, policy_)

#define KaSON_SCHEMA_INITIALIZER(struct_type_, fields_, lookup_slots_, \
		field_slots_, capacity_) \
	{ (fields_), (int)(sizeof(fields_) / sizeof((fields_)[0])), \
	  sizeof(struct_type_), (lookup_slots_), (field_slots_), (capacity_), \
	  { NULL, 0, 0 }, 0, 0 }

#define KaSON_SCHEMA_DEFINE(name_, struct_type_, fields_, capacity_) \
	static kason_lookup_key name_##_lookup_slots[(capacity_)]; \
	static uint16_t name_##_field_slots[(capacity_)]; \
	static kason_schema name_ = KaSON_SCHEMA_INITIALIZER(struct_type_, fields_, \
		name_##_lookup_slots, name_##_field_slots, capacity_)

/**
 * Validate and prepare a macro-defined schema. Initialize child schemas before
 * parents, once, before concurrent use. Schemas are immutable after success.
 * Key and string-default macro arguments must be literals or arrays. STRING,
 * STRING_U16, and STRING_U32 members are zero-terminated char, uint16_t, and
 * uint32_t arrays; their capacities are derived in the corresponding code
 * units. Array count members must have type size_t.
 * @ingroup kason_schema_unpack
 * @param schema Schema and backing lookup storage to initialize.
 * @return KaSON_SCHEMA_SUCCESS or KaSON_SCHEMA_ERROR_INVALID_SCHEMA.
 */
int kason_schema_init(kason_schema* schema);

/** Unpack one NUL-terminated JSON object.
 * @ingroup kason_schema_unpack
 * @param json Input object; slices are used only during this call.
 * @param schema Initialized destination schema.
 * @param output Destination structure. It is initialized from defaults before parsing.
 * @param error Optional detailed error destination; may be NULL.
 * @return KaSON_SCHEMA_SUCCESS or a KaSON_SCHEMA_ERROR_* value.
 */
int kason_unpack(char* json, const kason_schema* schema, void* output,
		kason_schema_error* error);
/** Unpack one JSON object from an inclusive range.
 * @ingroup kason_schema_unpack
 * @param begin Opening `{` byte. @param end Closing `}` byte, inclusive.
 * @param schema Initialized destination schema. @param output Destination structure.
 * @param error Optional detailed error destination; may be NULL.
 * @return KaSON_SCHEMA_SUCCESS or a KaSON_SCHEMA_ERROR_* value.
 */
int kason_unpack_range(char* begin, char* end, const kason_schema* schema,
		void* output, kason_schema_error* error);

/**
 * Flagged variants. KaSON_UNPACK_RELAXED returns after every schema field has
 * been decoded, so trailing syntax and duplicate fields are not validated.
 * @ingroup kason_schema_unpack
 * @param json NUL-terminated JSON object.
 * @param schema Initialized destination schema.
 * @param output Destination structure.
 * @param flags Bitwise combination of KaSON_UNPACK_* flags.
 * @param error Optional detailed error destination; may be NULL.
 * @return KaSON_SCHEMA_SUCCESS or a KaSON_SCHEMA_ERROR_* value.
 */
int kason_unpack_flags(char* json, const kason_schema* schema, void* output,
		unsigned flags, kason_schema_error* error);
/** Inclusive-range form of kason_unpack_flags().
 * @ingroup kason_schema_unpack
 * @param begin Opening `{` byte. @param end Closing `}` byte, inclusive.
 * @param schema Initialized destination schema. @param output Destination structure.
 * @param flags Bitwise combination of KaSON_UNPACK_* flags.
 * @param error Optional detailed error destination; may be NULL.
 * @return KaSON_SCHEMA_SUCCESS or a KaSON_SCHEMA_ERROR_* value.
 */
int kason_unpack_range_flags(char* begin, char* end, const kason_schema* schema,
		void* output, unsigned flags, kason_schema_error* error);

/** Initialize a writer targeting a fixed buffer.
 * @ingroup kason_writers
 * @param writer State to initialize. @param buffer Output buffer.
 * @param capacity Buffer bytes, including space for the trailing NUL.
 * @return KaSON_SCHEMA_SUCCESS or KaSON_SCHEMA_ERROR_INVALID_SCHEMA.
 */
int kason_writer_init_buffer(kason_writer* writer, char* buffer, size_t capacity);
/** Initialize a writer targeting an output callback.
 * @ingroup kason_writers
 * @param writer State to initialize.
 * @param scratch Optional staging storage; NULL only when capacity is zero.
 * @param scratch_capacity Staging capacity in bytes.
 * @param callback Output consumer; must not be NULL.
 * @param user_data Opaque pointer passed to @p callback.
 * @return KaSON_SCHEMA_SUCCESS or a KaSON_SCHEMA_ERROR_* value.
 */
int kason_writer_init_callback(kason_writer* writer,
		char* scratch, size_t scratch_capacity,
		kason_writer_callback callback, void* user_data);
/** Initialize a writer that only counts encoded bytes.
 * @ingroup kason_writers
 * @param writer State to initialize.
 * @return KaSON_SCHEMA_SUCCESS or KaSON_SCHEMA_ERROR_INVALID_SCHEMA.
 */
int kason_writer_init_counter(kason_writer* writer);
/** Reset length and status while retaining writer configuration.
 * @ingroup kason_writers
 * @param writer Previously initialized writer.
 */
void kason_writer_reset(kason_writer* writer);
/** Obtain encoded length accumulated by a writer.
 * @ingroup kason_writers
 * @param writer Initialized writer.
 * @return Encoded byte count excluding the buffer-mode NUL terminator.
 */
size_t kason_writer_length(const kason_writer* writer);

/**
 * Pack one JSON object in schema order. Buffer capacity includes the trailing
 * NUL byte; writer length excludes it. Callback output can be partial on error.
 * @ingroup kason_writers
 * @param writer Initialized output writer.
 * @param schema Initialized schema describing @p object.
 * @param object Source C structure.
 * @param flags Bitwise combination of KaSON_PACK_* flags.
 * @param error Optional detailed error destination; may be NULL.
 * @return KaSON_SCHEMA_SUCCESS or a KaSON_SCHEMA_ERROR_* value.
 */
int kason_pack(kason_writer* writer, const kason_schema* schema,
		const void* object, unsigned flags, kason_schema_error* error);

#ifdef __cplusplus
}
#endif

#endif
