#include "kason_schema.h"
#include "kason_internal.h"

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct s_kason_unpack_context {
	const kason_schema* schema;
	unsigned char* output;
	kason_schema_error* error;
	uint64_t found;
	uint64_t complete_mask;
	unsigned flags;
	int depth;
	int status;
} kason_unpack_context;

typedef struct s_kason_unpack_array_context {
	const kason_schema_field* field;
	unsigned char* output;
	kason_schema_error* error;
	size_t count;
	unsigned flags;
	int depth;
	int status;
} kason_unpack_array_context;

static void kason_schema_clear_error(kason_schema_error* error) {
	if (error != NULL) {
		error->code = KaSON_SCHEMA_SUCCESS;
		error->field = NULL;
		error->array_index = -1;
		error->depth = 0;
	}
}

static int kason_schema_set_error(kason_schema_error* error, int code,
		const kason_schema_field* field, int array_index, int depth) {
	if (error != NULL && error->code == KaSON_SCHEMA_SUCCESS) {
		error->code = code;
		error->field = field;
		error->array_index = array_index;
		error->depth = depth;
	}
	return code;
}

static int kason_schema_utf8_sequence(const unsigned char* value, size_t length,
		size_t offset, size_t* sequence_length) {
	unsigned char first = value[offset];
	uint32_t scalar;
	uint32_t minimum;
	size_t count;
	size_t i;

	if (first < 0x80) {
		*sequence_length = 1;
		return 1;
	}
	if (first >= 0xc2 && first <= 0xdf) {
		count = 2;
		scalar = first & 0x1f;
		minimum = 0x80;
	} else if (first >= 0xe0 && first <= 0xef) {
		count = 3;
		scalar = first & 0x0f;
		minimum = 0x800;
	} else if (first >= 0xf0 && first <= 0xf4) {
		count = 4;
		scalar = first & 0x07;
		minimum = 0x10000;
	} else {
		return 0;
	}
	if (count > length - offset) {
		return 0;
	}
	for (i = 1; i < count; i++) {
		unsigned char continuation = value[offset + i];
		if ((continuation & 0xc0) != 0x80) {
			return 0;
		}
		scalar = (scalar << 6) | (continuation & 0x3f);
	}
	if (scalar < minimum || scalar > 0x10ffff ||
			(scalar >= 0xd800 && scalar <= 0xdfff)) {
		return 0;
	}
	*sequence_length = count;
	return 1;
}

static int kason_schema_utf8_valid(const char* value, size_t length) {
	size_t offset = 0;

	if (value == NULL && length != 0) {
		return 0;
	}
	while (offset < length) {
		size_t sequence_length;
		if (!kason_schema_utf8_sequence(
				(const unsigned char*)value, length, offset, &sequence_length)) {
			return 0;
		}
		offset += sequence_length;
	}
	return 1;
}

static size_t kason_schema_type_size(int type) {
	switch (type) {
	case KaSON_SCHEMA_TYPE_BOOL:
	case KaSON_SCHEMA_TYPE_INT:
		return sizeof(int);
	case KaSON_SCHEMA_TYPE_INT64:
		return sizeof(int64_t);
	case KaSON_SCHEMA_TYPE_U32:
		return sizeof(uint32_t);
	case KaSON_SCHEMA_TYPE_U64:
		return sizeof(uint64_t);
	case KaSON_SCHEMA_TYPE_DOUBLE:
		return sizeof(double);
	default:
		return 0;
	}
}

static size_t kason_schema_bounded_string_length(const char* value, size_t capacity) {
	size_t length;
	for (length = 0; length < capacity; length++) {
		if (value[length] == '\0') {
			return length;
		}
	}
	return SIZE_MAX;
}

static size_t kason_schema_bounded_u16_length(const uint16_t* value,
		size_t capacity) {
	size_t length;
	for (length = 0; length < capacity; length++) {
		if (value[length] == 0) {
			return length;
		}
	}
	return SIZE_MAX;
}

static size_t kason_schema_bounded_u32_length(const uint32_t* value,
		size_t capacity) {
	size_t length;
	for (length = 0; length < capacity; length++) {
		if (value[length] == 0) {
			return length;
		}
	}
	return SIZE_MAX;
}

static int kason_schema_utf16_valid(const uint16_t* value, size_t length) {
	size_t index = 0;
	while (index < length) {
		uint16_t unit = value[index++];
		if (unit == 0 || (unit >= 0xdc00 && unit <= 0xdfff)) {
			return 0;
		}
		if (unit >= 0xd800 && unit <= 0xdbff) {
			if (index >= length || value[index] < 0xdc00 || value[index] > 0xdfff) {
				return 0;
			}
			index++;
		}
	}
	return 1;
}

static int kason_schema_utf32_valid(const uint32_t* value, size_t length) {
	size_t index;
	for (index = 0; index < length; index++) {
		uint32_t scalar = value[index];
		if (scalar == 0 || scalar > 0x10ffff ||
				(scalar >= 0xd800 && scalar <= 0xdfff)) {
			return 0;
		}
	}
	return 1;
}

static int kason_schema_object_valid(const kason_schema* schema,
		const unsigned char* object, int depth) {
	int i;

	if (schema == NULL || object == NULL || depth >= KaSON_MAX_NESTING) {
		return 0;
	}
	for (i = 0; i < schema->field_count; i++) {
		const kason_schema_field* field = &schema->fields[i];
		const unsigned char* value = object + field->offset;
		size_t count = 1;
		size_t item;

		if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
			memcpy(&count, object + field->count_offset, sizeof(count));
			if (count > field->capacity) {
				return 0;
			}
		}
		for (item = 0; item < count; item++) {
			const unsigned char* element = value + item * field->element_size;
			if (field->type == KaSON_SCHEMA_TYPE_BOOL) {
				int actual;
				memcpy(&actual, element, sizeof(actual));
				if (actual != 0 && actual != 1) {
					return 0;
				}
			} else if (field->type == KaSON_SCHEMA_TYPE_DOUBLE) {
				double actual;
				memcpy(&actual, element, sizeof(actual));
				if (!isfinite(actual)) {
					return 0;
				}
			} else if (field->type == KaSON_SCHEMA_TYPE_STRING) {
				size_t capacity = (field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0
					? field->element_size : field->size;
				size_t length = kason_schema_bounded_string_length(
					(const char*)element, capacity);
				if (length == SIZE_MAX || !kason_schema_utf8_valid(
						(const char*)element, length)) {
					return 0;
				}
			} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U16) {
				size_t bytes = (field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0
					? field->element_size : field->size;
				size_t capacity = bytes / sizeof(uint16_t);
				size_t length = kason_schema_bounded_u16_length(
					(const uint16_t*)element, capacity);
				if (length == SIZE_MAX || !kason_schema_utf16_valid(
						(const uint16_t*)element, length)) {
					return 0;
				}
			} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U32) {
				size_t bytes = (field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0
					? field->element_size : field->size;
				size_t capacity = bytes / sizeof(uint32_t);
				size_t length = kason_schema_bounded_u32_length(
					(const uint32_t*)element, capacity);
				if (length == SIZE_MAX || !kason_schema_utf32_valid(
						(const uint32_t*)element, length)) {
					return 0;
				}
			} else if (field->type == KaSON_SCHEMA_TYPE_STRUCT &&
					!kason_schema_object_valid(field->child_schema,
						element, depth + 1)) {
				return 0;
			}
		}
	}
	return 1;
}

static int kason_schema_validate_default(const kason_schema_field* field) {
	const kason_schema_policy* policy = &field->policy;

	if ((policy->flags & KaSON_SCHEMA_POLICY_DEFAULT) == 0) {
		return 1;
	}
	if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		return policy->pointer_value == NULL && policy->value_size == 0;
	}
	switch (field->type) {
	case KaSON_SCHEMA_TYPE_BOOL:
		return policy->signed_value == 0 || policy->signed_value == 1;
	case KaSON_SCHEMA_TYPE_INT:
		return policy->signed_value >= INT_MIN && policy->signed_value <= INT_MAX;
	case KaSON_SCHEMA_TYPE_INT64:
		return 1;
	case KaSON_SCHEMA_TYPE_U32:
		return policy->unsigned_value <= UINT32_MAX;
	case KaSON_SCHEMA_TYPE_U64:
		return 1;
	case KaSON_SCHEMA_TYPE_DOUBLE:
		return isfinite(policy->real_value) != 0;
	case KaSON_SCHEMA_TYPE_STRING:
		return policy->pointer_value != NULL &&
			policy->value_size < field->size &&
			((const char*)policy->pointer_value)[policy->value_size] == '\0' &&
			memchr(policy->pointer_value, '\0', policy->value_size) == NULL &&
			kason_schema_utf8_valid((const char*)policy->pointer_value,
				policy->value_size);
	case KaSON_SCHEMA_TYPE_STRING_U16:
		return policy->pointer_value != NULL &&
			policy->value_size < field->size / sizeof(uint16_t) &&
			((const uint16_t*)policy->pointer_value)[policy->value_size] == 0 &&
			kason_schema_utf16_valid((const uint16_t*)policy->pointer_value,
				policy->value_size);
	case KaSON_SCHEMA_TYPE_STRING_U32:
		return policy->pointer_value != NULL &&
			policy->value_size < field->size / sizeof(uint32_t) &&
			((const uint32_t*)policy->pointer_value)[policy->value_size] == 0 &&
			kason_schema_utf32_valid((const uint32_t*)policy->pointer_value,
				policy->value_size);
	case KaSON_SCHEMA_TYPE_STRUCT:
		return policy->pointer_value != NULL && policy->value_size == field->size &&
			kason_schema_object_valid(field->child_schema,
				(const unsigned char*)policy->pointer_value, 0);
	default:
		return 0;
	}
}

static int kason_schema_validate_field(const kason_schema* schema,
		const kason_schema_field* field) {
	unsigned policy_flags = field->policy.flags;
	size_t expected_size;

	if (field->json_key == NULL || field->key_length < 0 ||
			!kason_schema_utf8_valid(field->json_key, (size_t)field->key_length) ||
			field->offset > schema->object_size ||
			field->size > schema->object_size - field->offset ||
			(policy_flags != KaSON_SCHEMA_POLICY_REQUIRED &&
			 policy_flags != KaSON_SCHEMA_POLICY_DEFAULT)) {
		return 0;
	}
	if (field->type < KaSON_SCHEMA_TYPE_BOOL ||
			field->type > KaSON_SCHEMA_TYPE_STRING_U32) {
		return 0;
	}
	if ((field->flags & ~KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		return 0;
	}
	if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
		if (field->child_schema == NULL || !field->child_schema->initialized ||
				field->element_size != field->child_schema->object_size) {
			return 0;
		}
	} else if (field->child_schema != NULL) {
		return 0;
	}

	if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		if (field->capacity == 0 || field->element_size == 0 ||
				field->count_offset == KaSON_SCHEMA_NO_OFFSET ||
				field->count_size != sizeof(size_t) ||
				field->count_offset > schema->object_size ||
				field->count_size > schema->object_size - field->count_offset ||
				field->capacity > SIZE_MAX / field->element_size ||
				field->capacity * field->element_size != field->size) {
			return 0;
		}
		if (field->type == KaSON_SCHEMA_TYPE_STRING) {
			if (field->element_size == 0) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U16) {
			if (field->element_size < sizeof(uint16_t) ||
					field->element_size % sizeof(uint16_t) != 0) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U32) {
			if (field->element_size < sizeof(uint32_t) ||
					field->element_size % sizeof(uint32_t) != 0) {
				return 0;
			}
		} else if (field->type != KaSON_SCHEMA_TYPE_STRUCT) {
			expected_size = kason_schema_type_size(field->type);
			if (expected_size == 0 || field->element_size != expected_size) {
				return 0;
			}
		}
	} else {
		if (field->count_offset != KaSON_SCHEMA_NO_OFFSET || field->capacity != 1) {
			return 0;
		}
		if (field->type == KaSON_SCHEMA_TYPE_STRING) {
			if (field->size == 0) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U16) {
			if (field->size < sizeof(uint16_t) ||
					field->size % sizeof(uint16_t) != 0) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U32) {
			if (field->size < sizeof(uint32_t) ||
					field->size % sizeof(uint32_t) != 0) {
				return 0;
			}
		} else if (field->type != KaSON_SCHEMA_TYPE_STRUCT) {
			expected_size = kason_schema_type_size(field->type);
			if (expected_size == 0 || field->size != expected_size) {
				return 0;
			}
		}
	}
	return kason_schema_validate_default(field);
}

int kason_schema_init(kason_schema* schema) {
	int i;

	if (schema == NULL || schema->fields == NULL || schema->field_count <= 0 ||
			schema->field_count > KaSON_SCHEMA_MAX_FIELDS || schema->object_size == 0 ||
			schema->lookup_slots == NULL || schema->slot_field_indices == NULL ||
			schema->lookup_capacity < schema->field_count) {
		return KaSON_SCHEMA_ERROR_INVALID_SCHEMA;
	}
	schema->initialized = 0;
	schema->required_mask = 0;
	if (kason_lookup_table_init(&schema->lookup, schema->lookup_slots,
			schema->lookup_capacity) != KaSON_PARSE_RESULT_SUCCESS) {
		return KaSON_SCHEMA_ERROR_INVALID_SCHEMA;
	}
	for (i = 0; i < schema->lookup_capacity; i++) {
		schema->slot_field_indices[i] = KaSON_SCHEMA_NO_FIELD;
	}
	for (i = 0; i < schema->field_count; i++) {
		const kason_schema_field* field = &schema->fields[i];
		int slot_index = -1;
		int slot;

		if (!kason_schema_validate_field(schema, field) ||
				kason_lookup_table_add(&schema->lookup, field->json_key,
					field->key_length) != KaSON_PARSE_RESULT_SUCCESS) {
			return KaSON_SCHEMA_ERROR_INVALID_SCHEMA;
		}
		for (slot = 0; slot < schema->lookup_capacity; slot++) {
			if (schema->lookup_slots[slot].value == field->json_key &&
					schema->lookup_slots[slot].length == field->key_length) {
				slot_index = slot;
				break;
			}
		}
		if (slot_index < 0) {
			return KaSON_SCHEMA_ERROR_INVALID_SCHEMA;
		}
		schema->slot_field_indices[slot_index] = (uint16_t)i;
		if ((field->policy.flags & KaSON_SCHEMA_POLICY_REQUIRED) != 0) {
			schema->required_mask |= UINT64_C(1) << i;
		}
	}
	schema->initialized = 1;
	return KaSON_SCHEMA_SUCCESS;
}

static void kason_schema_store_default(const kason_schema_field* field,
		unsigned char* output) {
	unsigned char* destination = output + field->offset;

	if ((field->policy.flags & KaSON_SCHEMA_POLICY_DEFAULT) == 0) {
		return;
	}
	if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		size_t zero = 0;
		memcpy(output + field->count_offset, &zero, sizeof(zero));
		return;
	}
	switch (field->type) {
	case KaSON_SCHEMA_TYPE_BOOL:
	case KaSON_SCHEMA_TYPE_INT: {
		int value = (int)field->policy.signed_value;
		memcpy(destination, &value, sizeof(value));
		break;
	}
	case KaSON_SCHEMA_TYPE_INT64: {
		int64_t value = field->policy.signed_value;
		memcpy(destination, &value, sizeof(value));
		break;
	}
	case KaSON_SCHEMA_TYPE_U32: {
		uint32_t value = (uint32_t)field->policy.unsigned_value;
		memcpy(destination, &value, sizeof(value));
		break;
	}
	case KaSON_SCHEMA_TYPE_U64: {
		uint64_t value = field->policy.unsigned_value;
		memcpy(destination, &value, sizeof(value));
		break;
	}
	case KaSON_SCHEMA_TYPE_DOUBLE: {
		double value = field->policy.real_value;
		memcpy(destination, &value, sizeof(value));
		break;
	}
	case KaSON_SCHEMA_TYPE_STRING:
		memcpy(destination, field->policy.pointer_value,
			field->policy.value_size);
		destination[field->policy.value_size] = '\0';
		break;
	case KaSON_SCHEMA_TYPE_STRING_U16: {
		uint16_t zero = 0;
		memcpy(destination, field->policy.pointer_value,
			field->policy.value_size * sizeof(uint16_t));
		memcpy(destination + field->policy.value_size * sizeof(uint16_t),
			&zero, sizeof(zero));
		break;
	}
	case KaSON_SCHEMA_TYPE_STRING_U32: {
		uint32_t zero = 0;
		memcpy(destination, field->policy.pointer_value,
			field->policy.value_size * sizeof(uint32_t));
		memcpy(destination + field->policy.value_size * sizeof(uint32_t),
			&zero, sizeof(zero));
		break;
	}
	case KaSON_SCHEMA_TYPE_STRUCT:
		memcpy(destination, field->policy.pointer_value, field->size);
		break;
	default:
		break;
	}
}

static void kason_schema_apply_defaults(const kason_schema* schema,
		unsigned char* output) {
	int i;
	for (i = 0; i < schema->field_count; i++) {
		kason_schema_store_default(&schema->fields[i], output);
	}
}

static int kason_schema_conversion_error(int conversion_result) {
	return conversion_result == KaSON_CONVERT_RANGE
		? KaSON_SCHEMA_ERROR_RANGE
		: KaSON_SCHEMA_ERROR_TYPE;
}

static int kason_schema_store_scalar(const kason_schema_field* field,
		unsigned char* destination, size_t capacity, const kason_data* data,
		kason_schema_error* error, int array_index, int depth) {
	int conversion;

	switch (field->type) {
	case KaSON_SCHEMA_TYPE_BOOL: {
		int value;
		if (data->type == KaSON_TYPE_TRUE) {
			value = 1;
		} else if (data->type == KaSON_TYPE_FALSE) {
			value = 0;
		} else {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_INT: {
		int value;
		conversion = kason_value_to_int(data->type, data->begin, data->end, &value);
		if (conversion != KaSON_CONVERT_SUCCESS) {
			return kason_schema_set_error(error,
				kason_schema_conversion_error(conversion), field, array_index, depth);
		}
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_INT64: {
		int64_t value;
		conversion = kason_value_to_int64(data->type, data->begin, data->end, &value);
		if (conversion != KaSON_CONVERT_SUCCESS) {
			return kason_schema_set_error(error,
				kason_schema_conversion_error(conversion), field, array_index, depth);
		}
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_U32: {
		uint64_t parsed;
		uint32_t value;
		conversion = kason_value_to_uint64(data->type, data->begin, data->end, &parsed);
		if (conversion != KaSON_CONVERT_SUCCESS || parsed > UINT32_MAX) {
			return kason_schema_set_error(error,
				conversion == KaSON_CONVERT_SUCCESS ? KaSON_SCHEMA_ERROR_RANGE
					: kason_schema_conversion_error(conversion),
				field, array_index, depth);
		}
		value = (uint32_t)parsed;
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_U64: {
		uint64_t value;
		conversion = kason_value_to_uint64(data->type, data->begin, data->end, &value);
		if (conversion != KaSON_CONVERT_SUCCESS) {
			return kason_schema_set_error(error,
				kason_schema_conversion_error(conversion), field, array_index, depth);
		}
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_DOUBLE: {
		double value;
		conversion = kason_value_to_double(data->type, data->begin, data->end, &value);
		if (conversion != KaSON_CONVERT_SUCCESS || !isfinite(value)) {
			return kason_schema_set_error(error,
				conversion == KaSON_CONVERT_RANGE ? KaSON_SCHEMA_ERROR_RANGE
					: KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_STRING: {
		int length;
		if (data->type != KaSON_TYPE_STRING) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		length = kason_strlen(data->begin, data->end);
		if (length < 0) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		if ((size_t)length >= capacity) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_STRING_CAPACITY,
				field, array_index, depth);
		}
		if (kason_strcpy(data->begin, data->end, (char*)destination, length) != length) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		if (memchr(destination, '\0', (size_t)length) != NULL) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		destination[length] = '\0';
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_STRING_U16: {
		int length;
		size_t unit_capacity = capacity / sizeof(uint16_t);
		uint16_t zero = 0;
		if (data->type != KaSON_TYPE_STRING) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		length = kason_strcpy_utf16(data->begin, data->end, NULL, 0);
		if (length < 0) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		if ((size_t)length >= unit_capacity) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_STRING_CAPACITY,
				field, array_index, depth);
		}
		if (kason_strcpy_utf16(data->begin, data->end, (uint16_t*)destination,
				length) != length || !kason_schema_utf16_valid(
					(const uint16_t*)destination, (size_t)length)) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		memcpy(destination + (size_t)length * sizeof(uint16_t), &zero, sizeof(zero));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_STRING_U32: {
		int length;
		size_t unit_capacity = capacity / sizeof(uint32_t);
		uint32_t zero = 0;
		if (data->type != KaSON_TYPE_STRING) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		length = kason_strcpy_utf32(data->begin, data->end, NULL, 0);
		if (length < 0) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		if ((size_t)length >= unit_capacity) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_STRING_CAPACITY,
				field, array_index, depth);
		}
		if (kason_strcpy_utf32(data->begin, data->end, (uint32_t*)destination,
				length) != length || !kason_schema_utf32_valid(
					(const uint32_t*)destination, (size_t)length)) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		memcpy(destination + (size_t)length * sizeof(uint32_t), &zero, sizeof(zero));
		return KaSON_SCHEMA_SUCCESS;
	}
	default:
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_INVALID_SCHEMA,
			field, array_index, depth);
	}
}

static int kason_unpack_range_internal(char* begin, char* end,
		const kason_schema* schema, unsigned char* output,
		kason_schema_error* error, unsigned flags, int depth);

static int kason_unpack_array_callback(kason_key* key, kason_data* data,
		int count, void* user_data) {
	kason_unpack_array_context* context = (kason_unpack_array_context*)user_data;
	const kason_schema_field* field = context->field;
	unsigned char* destination;
	int result;

	(void)key;
	(void)count;
	if (context->count >= field->capacity) {
		context->status = kason_schema_set_error(context->error,
			KaSON_SCHEMA_ERROR_ARRAY_CAPACITY, field, (int)context->count,
			context->depth);
		return KaSON_CALLBACK_BREAK;
	}
	destination = context->output + field->offset +
		context->count * field->element_size;
	if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
		if (data->type != KaSON_TYPE_OBJECT) {
			context->status = kason_schema_set_error(context->error,
				KaSON_SCHEMA_ERROR_TYPE, field, (int)context->count, context->depth);
			return KaSON_CALLBACK_BREAK;
		}
		result = kason_unpack_range_internal(data->begin, data->end,
			field->child_schema, destination, context->error, context->flags,
			context->depth + 1);
	} else {
		result = kason_schema_store_scalar(field, destination, field->element_size,
			data, context->error, (int)context->count, context->depth);
	}
	if (result != KaSON_SCHEMA_SUCCESS) {
		context->status = result;
		return KaSON_CALLBACK_BREAK;
	}
	context->count++;
	return KaSON_CALLBACK_CONTINUE;
}

static int kason_unpack_array(const kason_schema_field* field, kason_data* data,
		unsigned char* output, kason_schema_error* error, unsigned flags,
		int depth) {
	kason_unpack_array_context context;
	size_t count;
	int parse_result;

	if (data->type != KaSON_TYPE_ARRAY) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
			field, -1, depth);
	}
	context.field = field;
	context.output = output;
	context.error = error;
	context.count = 0;
	context.flags = flags;
	context.depth = depth;
	context.status = KaSON_SCHEMA_SUCCESS;
	parse_result = kason_parse_container(data, kason_unpack_array_callback, &context);
	if (context.status != KaSON_SCHEMA_SUCCESS) {
		return context.status;
	}
	if (parse_result != KaSON_PARSE_RESULT_SUCCESS) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR,
			field, -1, depth);
	}
	count = context.count;
	memcpy(output + field->count_offset, &count, sizeof(count));
	return KaSON_SCHEMA_SUCCESS;
}

static int kason_schema_conversion_selector(
		const kason_lookup_key* matched_key, void* user_data) {
	kason_unpack_context* context = (kason_unpack_context*)user_data;
	ptrdiff_t slot = matched_key - context->schema->lookup_slots;
	uint16_t field_index;
	const kason_schema_field* field;

	if (slot < 0 || slot >= context->schema->lookup_capacity) {
		return KaSON_CACHED_NUMBER_NONE;
	}
	field_index = context->schema->slot_field_indices[slot];
	if (field_index == KaSON_SCHEMA_NO_FIELD ||
			field_index >= context->schema->field_count) {
		return KaSON_CACHED_NUMBER_NONE;
	}
	field = &context->schema->fields[field_index];
	if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		return KaSON_CACHED_NUMBER_NONE;
	}
	switch (field->type) {
	case KaSON_SCHEMA_TYPE_INT:
	case KaSON_SCHEMA_TYPE_INT64:
		return KaSON_CACHED_NUMBER_SIGNED;
	case KaSON_SCHEMA_TYPE_U32:
	case KaSON_SCHEMA_TYPE_U64:
		return KaSON_CACHED_NUMBER_UNSIGNED;
	case KaSON_SCHEMA_TYPE_DOUBLE:
		return KaSON_CACHED_NUMBER_DOUBLE;
	default:
		return KaSON_CACHED_NUMBER_NONE;
	}
}

static int kason_schema_store_cached_number(const kason_schema_field* field,
		unsigned char* destination, const kason_cached_number* cached,
		kason_schema_error* error, int depth) {
	if (cached->result != KaSON_CONVERT_SUCCESS) {
		return kason_schema_set_error(error,
			kason_schema_conversion_error(cached->result), field, -1, depth);
	}
	switch (field->type) {
	case KaSON_SCHEMA_TYPE_INT: {
		int64_t parsed = cached->value.signed_value;
		int value;
		if (cached->kind != KaSON_CACHED_NUMBER_SIGNED ||
				parsed < INT_MIN || parsed > INT_MAX) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_RANGE,
				field, -1, depth);
		}
		value = (int)parsed;
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_INT64:
		if (cached->kind != KaSON_CACHED_NUMBER_SIGNED) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, -1, depth);
		}
		memcpy(destination, &cached->value.signed_value,
			sizeof(cached->value.signed_value));
		return KaSON_SCHEMA_SUCCESS;
	case KaSON_SCHEMA_TYPE_U32: {
		uint64_t parsed = cached->value.unsigned_value;
		uint32_t value;
		if (cached->kind != KaSON_CACHED_NUMBER_UNSIGNED || parsed > UINT32_MAX) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_RANGE,
				field, -1, depth);
		}
		value = (uint32_t)parsed;
		memcpy(destination, &value, sizeof(value));
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_U64:
		if (cached->kind != KaSON_CACHED_NUMBER_UNSIGNED) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, -1, depth);
		}
		memcpy(destination, &cached->value.unsigned_value,
			sizeof(cached->value.unsigned_value));
		return KaSON_SCHEMA_SUCCESS;
	case KaSON_SCHEMA_TYPE_DOUBLE:
		if (cached->kind != KaSON_CACHED_NUMBER_DOUBLE ||
				!isfinite(cached->value.real_value)) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, -1, depth);
		}
		memcpy(destination, &cached->value.real_value,
			sizeof(cached->value.real_value));
		return KaSON_SCHEMA_SUCCESS;
	default:
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_INVALID_SCHEMA,
			field, -1, depth);
	}
}

static int kason_unpack_callback(const kason_lookup_key* matched_key,
		kason_data* data, int count, const kason_cached_number* cached_number,
		void* user_data) {
	kason_unpack_context* context = (kason_unpack_context*)user_data;
	ptrdiff_t slot = matched_key - context->schema->lookup_slots;
	uint16_t field_index;
	const kason_schema_field* field;
	unsigned char* destination;
	uint64_t mask;
	int result;

	(void)count;
	if (slot < 0 || slot >= context->schema->lookup_capacity) {
		context->status = kason_schema_set_error(context->error,
			KaSON_SCHEMA_ERROR_INVALID_SCHEMA, NULL, -1, context->depth);
		return KaSON_CALLBACK_BREAK;
	}
	field_index = context->schema->slot_field_indices[slot];
	if (field_index == KaSON_SCHEMA_NO_FIELD || field_index >= context->schema->field_count) {
		context->status = kason_schema_set_error(context->error,
			KaSON_SCHEMA_ERROR_INVALID_SCHEMA, NULL, -1, context->depth);
		return KaSON_CALLBACK_BREAK;
	}
	field = &context->schema->fields[field_index];
	mask = UINT64_C(1) << field_index;
	if ((context->found & mask) != 0) {
		context->status = kason_schema_set_error(context->error,
			KaSON_SCHEMA_ERROR_DUPLICATE_FIELD, field, -1, context->depth);
		return KaSON_CALLBACK_BREAK;
	}
	destination = context->output + field->offset;
	if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		result = kason_unpack_array(field, data, context->output,
			context->error, context->flags, context->depth);
	} else if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
		if (data->type != KaSON_TYPE_OBJECT) {
			result = kason_schema_set_error(context->error, KaSON_SCHEMA_ERROR_TYPE,
				field, -1, context->depth);
		} else {
			result = kason_unpack_range_internal(data->begin, data->end,
				field->child_schema, destination, context->error, context->flags,
				context->depth + 1);
		}
	} else if (cached_number != NULL &&
			cached_number->kind != KaSON_CACHED_NUMBER_NONE) {
		result = kason_schema_store_cached_number(field, destination,
			cached_number, context->error, context->depth);
	} else {
		result = kason_schema_store_scalar(field, destination, field->size,
			data, context->error, -1, context->depth);
	}
	if (result != KaSON_SCHEMA_SUCCESS) {
		context->status = result;
		return KaSON_CALLBACK_BREAK;
	}
	context->found |= mask;
	if ((context->flags & KaSON_UNPACK_RELAXED) != 0 &&
			context->found == context->complete_mask) {
		return KaSON_CALLBACK_BREAK;
	}
	return KaSON_CALLBACK_CONTINUE;
}

static int kason_unpack_range_internal(char* begin, char* end,
		const kason_schema* schema, unsigned char* output,
		kason_schema_error* error, unsigned flags, int depth) {
	kason_unpack_context context;
	uint64_t missing;
	int parse_result;
	int i;
	char* root = begin;

	if (depth >= KaSON_MAX_NESTING) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_NESTING,
			NULL, -1, depth);
	}
	if (schema == NULL || !schema->initialized || output == NULL ||
			(flags & ~KaSON_UNPACK_RELAXED) != 0) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_INVALID_SCHEMA,
			NULL, -1, depth);
	}
	while ((end == NULL ? *root != '\0' : root <= end) &&
			(*root == ' ' || *root == '\n' || *root == '\r' || *root == '\t')) {
		root++;
	}
	if ((end == NULL ? *root == '\0' : root > end) || *root != '{') {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
			NULL, -1, depth);
	}
	kason_schema_apply_defaults(schema, output);
	context.schema = schema;
	context.output = output;
	context.error = error;
	context.found = 0;
	context.complete_mask = schema->field_count == KaSON_SCHEMA_MAX_FIELDS
		? UINT64_MAX
		: (UINT64_C(1) << schema->field_count) - 1U;
	context.flags = flags;
	context.depth = depth;
	context.status = KaSON_SCHEMA_SUCCESS;
	parse_result = end == NULL
		? kason_parse_selected_converted(begin, &schema->lookup,
			kason_schema_conversion_selector, kason_unpack_callback, &context)
		: kason_parse_range_selected_converted(begin, end, &schema->lookup,
			kason_schema_conversion_selector, kason_unpack_callback, &context);
	if (context.status != KaSON_SCHEMA_SUCCESS) {
		return context.status;
	}
	if (parse_result != KaSON_PARSE_RESULT_SUCCESS) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR,
			NULL, -1, depth);
	}
	missing = schema->required_mask & ~context.found;
	if (missing != 0) {
		for (i = 0; i < schema->field_count; i++) {
			if ((missing & (UINT64_C(1) << i)) != 0) {
				return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_MISSING_FIELD,
					&schema->fields[i], -1, depth);
			}
		}
	}
	return KaSON_SCHEMA_SUCCESS;
}

int kason_unpack(char* json, const kason_schema* schema, void* output,
		kason_schema_error* error) {
	return kason_unpack_flags(json, schema, output, 0, error);
}

int kason_unpack_flags(char* json, const kason_schema* schema, void* output,
		unsigned flags, kason_schema_error* error) {
	kason_schema_clear_error(error);
	if (json == NULL) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR,
			NULL, -1, 0);
	}
	return kason_unpack_range_internal(json, NULL, schema,
		(unsigned char*)output, error, flags, 0);
}

int kason_unpack_range(char* begin, char* end, const kason_schema* schema,
		void* output, kason_schema_error* error) {
	return kason_unpack_range_flags(begin, end, schema, output, 0, error);
}

int kason_unpack_range_flags(char* begin, char* end, const kason_schema* schema,
		void* output, unsigned flags, kason_schema_error* error) {
	kason_schema_clear_error(error);
	if (begin == NULL || end == NULL || end < begin) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR,
			NULL, -1, 0);
	}
	return kason_unpack_range_internal(begin, end, schema,
		(unsigned char*)output, error, flags, 0);
}

static void kason_writer_zero(kason_writer* writer) {
	writer->buffer = NULL;
	writer->capacity = 0;
	writer->length = 0;
	writer->scratch = NULL;
	writer->scratch_capacity = 0;
	writer->scratch_length = 0;
	writer->callback = NULL;
	writer->user_data = NULL;
	writer->status = KaSON_SCHEMA_SUCCESS;
}

int kason_writer_init_buffer(kason_writer* writer, char* buffer, size_t capacity) {
	if (writer == NULL || buffer == NULL || capacity == 0) {
		return KaSON_SCHEMA_ERROR;
	}
	kason_writer_zero(writer);
	writer->mode = KaSON_WRITER_MODE_BUFFER;
	writer->buffer = buffer;
	writer->capacity = capacity;
	buffer[0] = '\0';
	return KaSON_SCHEMA_SUCCESS;
}

int kason_writer_init_callback(kason_writer* writer,
		char* scratch, size_t scratch_capacity,
		kason_writer_callback callback, void* user_data) {
	if (writer == NULL || callback == NULL ||
			(scratch == NULL && scratch_capacity != 0)) {
		return KaSON_SCHEMA_ERROR;
	}
	kason_writer_zero(writer);
	writer->mode = KaSON_WRITER_MODE_CALLBACK;
	writer->scratch = scratch;
	writer->scratch_capacity = scratch_capacity;
	writer->callback = callback;
	writer->user_data = user_data;
	return KaSON_SCHEMA_SUCCESS;
}

int kason_writer_init_counter(kason_writer* writer) {
	if (writer == NULL) {
		return KaSON_SCHEMA_ERROR;
	}
	kason_writer_zero(writer);
	writer->mode = KaSON_WRITER_MODE_COUNTER;
	return KaSON_SCHEMA_SUCCESS;
}

void kason_writer_reset(kason_writer* writer) {
	if (writer == NULL) {
		return;
	}
	writer->length = 0;
	writer->scratch_length = 0;
	writer->status = KaSON_SCHEMA_SUCCESS;
	if (writer->mode == KaSON_WRITER_MODE_BUFFER && writer->buffer != NULL &&
			writer->capacity > 0) {
		writer->buffer[0] = '\0';
	}
}

size_t kason_writer_length(const kason_writer* writer) {
	return writer == NULL ? 0 : writer->length;
}

static int kason_writer_flush(kason_writer* writer) {
	if (writer->mode != KaSON_WRITER_MODE_CALLBACK || writer->scratch_length == 0) {
		return KaSON_SCHEMA_SUCCESS;
	}
	if (writer->callback(writer->scratch, writer->scratch_length,
			writer->user_data) != 0) {
		writer->status = KaSON_SCHEMA_ERROR_WRITER_CALLBACK;
		return writer->status;
	}
	writer->scratch_length = 0;
	return KaSON_SCHEMA_SUCCESS;
}

static int kason_writer_write(kason_writer* writer, const char* data, size_t length) {
	if (writer->status != KaSON_SCHEMA_SUCCESS) {
		return writer->status;
	}
	if (length > SIZE_MAX - writer->length) {
		writer->status = KaSON_SCHEMA_ERROR_WRITER_CAPACITY;
		return writer->status;
	}
	if (writer->mode == KaSON_WRITER_MODE_COUNTER) {
		writer->length += length;
		return KaSON_SCHEMA_SUCCESS;
	}
	if (writer->mode == KaSON_WRITER_MODE_BUFFER) {
		if (length >= writer->capacity - writer->length) {
			writer->status = KaSON_SCHEMA_ERROR_WRITER_CAPACITY;
			return writer->status;
		}
		memcpy(writer->buffer + writer->length, data, length);
		writer->length += length;
		writer->buffer[writer->length] = '\0';
		return KaSON_SCHEMA_SUCCESS;
	}
	if (writer->mode != KaSON_WRITER_MODE_CALLBACK || writer->callback == NULL) {
		writer->status = KaSON_SCHEMA_ERROR;
		return writer->status;
	}
	if (writer->scratch_capacity == 0) {
		if (length != 0 && writer->callback(data, length, writer->user_data) != 0) {
			writer->status = KaSON_SCHEMA_ERROR_WRITER_CALLBACK;
			return writer->status;
		}
		writer->length += length;
		return KaSON_SCHEMA_SUCCESS;
	}
	if (length > writer->scratch_capacity) {
		if (kason_writer_flush(writer) != KaSON_SCHEMA_SUCCESS ||
				writer->callback(data, length, writer->user_data) != 0) {
			writer->status = KaSON_SCHEMA_ERROR_WRITER_CALLBACK;
			return writer->status;
		}
		writer->length += length;
		return KaSON_SCHEMA_SUCCESS;
	}
	if (length > writer->scratch_capacity - writer->scratch_length &&
			kason_writer_flush(writer) != KaSON_SCHEMA_SUCCESS) {
		return writer->status;
	}
	memcpy(writer->scratch + writer->scratch_length, data, length);
	writer->scratch_length += length;
	writer->length += length;
	return KaSON_SCHEMA_SUCCESS;
}

static int kason_writer_char(kason_writer* writer, char value) {
	return kason_writer_write(writer, &value, 1);
}

static int kason_writer_json_scalar(kason_writer* writer, uint32_t scalar) {
	static const char hex[] = "0123456789abcdef";
	char encoded[4];
	size_t length;

	if (scalar == '"' || scalar == '\\') {
		char escaped[2] = {'\\', (char)scalar};
		return kason_writer_write(writer, escaped, 2);
	}
	if (scalar < 0x20) {
		const char* simple = NULL;
		char unicode[6] = {'\\', 'u', '0', '0', '0', '0'};
		switch (scalar) {
		case '\b': simple = "\\b"; break;
		case '\f': simple = "\\f"; break;
		case '\n': simple = "\\n"; break;
		case '\r': simple = "\\r"; break;
		case '\t': simple = "\\t"; break;
		default:
			unicode[4] = hex[scalar >> 4];
			unicode[5] = hex[scalar & 0x0f];
			break;
		}
		return kason_writer_write(writer, simple != NULL ? simple : unicode,
			simple != NULL ? 2 : 6);
	}
	if (scalar <= 0x7f) {
		encoded[0] = (char)scalar;
		length = 1;
	} else if (scalar <= 0x7ff) {
		encoded[0] = (char)(0xc0 | (scalar >> 6));
		encoded[1] = (char)(0x80 | (scalar & 0x3f));
		length = 2;
	} else if (scalar <= 0xffff && !(scalar >= 0xd800 && scalar <= 0xdfff)) {
		encoded[0] = (char)(0xe0 | (scalar >> 12));
		encoded[1] = (char)(0x80 | ((scalar >> 6) & 0x3f));
		encoded[2] = (char)(0x80 | (scalar & 0x3f));
		length = 3;
	} else if (scalar >= 0x10000 && scalar <= 0x10ffff) {
		encoded[0] = (char)(0xf0 | (scalar >> 18));
		encoded[1] = (char)(0x80 | ((scalar >> 12) & 0x3f));
		encoded[2] = (char)(0x80 | ((scalar >> 6) & 0x3f));
		encoded[3] = (char)(0x80 | (scalar & 0x3f));
		length = 4;
	} else {
		writer->status = KaSON_SCHEMA_ERROR_TYPE;
		return writer->status;
	}
	return kason_writer_write(writer, encoded, length);
}

static int kason_writer_json_string(kason_writer* writer,
		const char* value, size_t length) {
	static const char hex[] = "0123456789abcdef";
	size_t offset = 0;

	if (kason_writer_char(writer, '"') != KaSON_SCHEMA_SUCCESS) {
		return writer->status;
	}
	while (offset < length) {
		unsigned char c = (unsigned char)value[offset];
		if (c == '"' || c == '\\') {
			char escaped[2] = {'\\', (char)c};
			if (kason_writer_write(writer, escaped, 2) != KaSON_SCHEMA_SUCCESS) {
				return writer->status;
			}
			offset++;
		} else if (c < 0x20) {
			const char* simple = NULL;
			char unicode[6] = {'\\', 'u', '0', '0', '0', '0'};
			switch (c) {
			case '\b': simple = "\\b"; break;
			case '\f': simple = "\\f"; break;
			case '\n': simple = "\\n"; break;
			case '\r': simple = "\\r"; break;
			case '\t': simple = "\\t"; break;
			default:
				unicode[4] = hex[c >> 4];
				unicode[5] = hex[c & 0x0f];
				break;
			}
			if (kason_writer_write(writer, simple != NULL ? simple : unicode,
					simple != NULL ? 2 : 6) != KaSON_SCHEMA_SUCCESS) {
				return writer->status;
			}
			offset++;
		} else {
			size_t sequence_length;
			if (!kason_schema_utf8_sequence((const unsigned char*)value,
					length, offset, &sequence_length)) {
				writer->status = KaSON_SCHEMA_ERROR_TYPE;
				return writer->status;
			}
			if (kason_writer_write(writer, value + offset, sequence_length) !=
					KaSON_SCHEMA_SUCCESS) {
				return writer->status;
			}
			offset += sequence_length;
		}
	}
	return kason_writer_char(writer, '"');
}

static int kason_writer_json_string_u16(kason_writer* writer,
		const uint16_t* value, size_t capacity) {
	size_t length = kason_schema_bounded_u16_length(value, capacity);
	size_t index = 0;

	if (length == SIZE_MAX || !kason_schema_utf16_valid(value, length)) {
		writer->status = KaSON_SCHEMA_ERROR_TYPE;
		return writer->status;
	}
	if (kason_writer_char(writer, '"') != KaSON_SCHEMA_SUCCESS) {
		return writer->status;
	}
	while (index < length) {
		uint32_t scalar = value[index++];
		if (scalar >= 0xd800 && scalar <= 0xdbff) {
			uint32_t low = value[index++];
			scalar = UINT32_C(0x10000) +
				((scalar - UINT32_C(0xd800)) << 10) +
				(low - UINT32_C(0xdc00));
		}
		if (kason_writer_json_scalar(writer, scalar) != KaSON_SCHEMA_SUCCESS) {
			return writer->status;
		}
	}
	return kason_writer_char(writer, '"');
}

static int kason_writer_json_string_u32(kason_writer* writer,
		const uint32_t* value, size_t capacity) {
	size_t length = kason_schema_bounded_u32_length(value, capacity);
	size_t index;

	if (length == SIZE_MAX || !kason_schema_utf32_valid(value, length)) {
		writer->status = KaSON_SCHEMA_ERROR_TYPE;
		return writer->status;
	}
	if (kason_writer_char(writer, '"') != KaSON_SCHEMA_SUCCESS) {
		return writer->status;
	}
	for (index = 0; index < length; index++) {
		if (kason_writer_json_scalar(writer, value[index]) != KaSON_SCHEMA_SUCCESS) {
			return writer->status;
		}
	}
	return kason_writer_char(writer, '"');
}

static size_t kason_schema_unsigned_text(uint64_t value, char output[32]) {
	char reversed[32];
	size_t length = 0;
	size_t i;

	do {
		reversed[length++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0);
	for (i = 0; i < length; i++) {
		output[i] = reversed[length - i - 1];
	}
	return length;
}

static int kason_writer_unsigned(kason_writer* writer, uint64_t value) {
	char output[32];
	return kason_writer_write(writer, output,
		kason_schema_unsigned_text(value, output));
}

static int kason_writer_signed(kason_writer* writer, int64_t value) {
	uint64_t magnitude;
	char output[32];
	size_t length = 0;

	if (value < 0) {
		output[length++] = '-';
		magnitude = (uint64_t)(-(value + 1)) + 1U;
	} else {
		magnitude = (uint64_t)value;
	}
	length += kason_schema_unsigned_text(magnitude, output + length);
	return kason_writer_write(writer, output, length);
}

static int kason_writer_double(kason_writer* writer, double value) {
	char output[64];
	struct lconv* locale;
	const char* decimal;
	char* found;
	size_t decimal_length;
	int length;

	if (!isfinite(value)) {
		writer->status = KaSON_SCHEMA_ERROR_TYPE;
		return writer->status;
	}
	length = snprintf(output, sizeof(output), "%.17g", value);
	if (length <= 0 || (size_t)length >= sizeof(output)) {
		writer->status = KaSON_SCHEMA_ERROR_RANGE;
		return writer->status;
	}
	locale = localeconv();
	decimal = locale == NULL ? "." : locale->decimal_point;
	decimal_length = decimal == NULL ? 0 : strlen(decimal);
	if (decimal_length > 0 && !(decimal_length == 1 && decimal[0] == '.')) {
		found = strstr(output, decimal);
		if (found != NULL) {
			if (decimal_length > 1) {
				memmove(found + 1, found + decimal_length,
					(size_t)(output + length - (found + decimal_length)) + 1);
				length -= (int)(decimal_length - 1);
			}
			*found = '.';
		}
	}
	return kason_writer_write(writer, output, (size_t)length);
}

static int kason_schema_pack_object(kason_writer* writer,
		const kason_schema* schema, const unsigned char* object,
		unsigned flags, kason_schema_error* error, int depth);

static int kason_schema_objects_equal(const kason_schema* schema,
		const unsigned char* left, const unsigned char* right, int depth) {
	int i;

	if (depth >= KaSON_MAX_NESTING) {
		return 0;
	}
	for (i = 0; i < schema->field_count; i++) {
		const kason_schema_field* field = &schema->fields[i];
		const unsigned char* left_value = left + field->offset;
		const unsigned char* right_value = right + field->offset;

		if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
			size_t left_count;
			size_t right_count;
			size_t item;
			memcpy(&left_count, left + field->count_offset, sizeof(left_count));
			memcpy(&right_count, right + field->count_offset, sizeof(right_count));
			if (left_count != right_count || left_count > field->capacity) {
				return 0;
			}
			for (item = 0; item < left_count; item++) {
				const unsigned char* left_item = left_value + item * field->element_size;
				const unsigned char* right_item = right_value + item * field->element_size;
				if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
					if (!kason_schema_objects_equal(field->child_schema,
							left_item, right_item, depth + 1)) {
						return 0;
					}
				} else if (field->type == KaSON_SCHEMA_TYPE_STRING) {
					size_t left_length = kason_schema_bounded_string_length(
						(const char*)left_item, field->element_size);
					size_t right_length = kason_schema_bounded_string_length(
						(const char*)right_item, field->element_size);
					if (left_length == SIZE_MAX || left_length != right_length ||
							memcmp(left_item, right_item, left_length) != 0) {
						return 0;
					}
				} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U16) {
					size_t capacity = field->element_size / sizeof(uint16_t);
					size_t left_length = kason_schema_bounded_u16_length(
						(const uint16_t*)left_item, capacity);
					size_t right_length = kason_schema_bounded_u16_length(
						(const uint16_t*)right_item, capacity);
					if (left_length == SIZE_MAX || left_length != right_length ||
							memcmp(left_item, right_item,
								left_length * sizeof(uint16_t)) != 0) {
						return 0;
					}
				} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U32) {
					size_t capacity = field->element_size / sizeof(uint32_t);
					size_t left_length = kason_schema_bounded_u32_length(
						(const uint32_t*)left_item, capacity);
					size_t right_length = kason_schema_bounded_u32_length(
						(const uint32_t*)right_item, capacity);
					if (left_length == SIZE_MAX || left_length != right_length ||
							memcmp(left_item, right_item,
								left_length * sizeof(uint32_t)) != 0) {
						return 0;
					}
				} else if (memcmp(left_item, right_item, field->element_size) != 0) {
					return 0;
				}
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
			if (!kason_schema_objects_equal(field->child_schema,
					left_value, right_value, depth + 1)) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING) {
			size_t left_length = kason_schema_bounded_string_length(
				(const char*)left_value, field->size);
			size_t right_length = kason_schema_bounded_string_length(
				(const char*)right_value, field->size);
			if (left_length == SIZE_MAX || left_length != right_length ||
					memcmp(left_value, right_value, left_length) != 0) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U16) {
			size_t capacity = field->size / sizeof(uint16_t);
			size_t left_length = kason_schema_bounded_u16_length(
				(const uint16_t*)left_value, capacity);
			size_t right_length = kason_schema_bounded_u16_length(
				(const uint16_t*)right_value, capacity);
			if (left_length == SIZE_MAX || left_length != right_length ||
					memcmp(left_value, right_value,
						left_length * sizeof(uint16_t)) != 0) {
				return 0;
			}
		} else if (field->type == KaSON_SCHEMA_TYPE_STRING_U32) {
			size_t capacity = field->size / sizeof(uint32_t);
			size_t left_length = kason_schema_bounded_u32_length(
				(const uint32_t*)left_value, capacity);
			size_t right_length = kason_schema_bounded_u32_length(
				(const uint32_t*)right_value, capacity);
			if (left_length == SIZE_MAX || left_length != right_length ||
					memcmp(left_value, right_value,
						left_length * sizeof(uint32_t)) != 0) {
				return 0;
			}
		} else if (memcmp(left_value, right_value, field->size) != 0) {
			return 0;
		}
	}
	return 1;
}

static int kason_schema_field_is_default(const kason_schema_field* field,
		const unsigned char* object, int depth) {
	const unsigned char* value = object + field->offset;

	(void)depth;
	if ((field->policy.flags & KaSON_SCHEMA_POLICY_DEFAULT) == 0) {
		return 0;
	}
	if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
		size_t count;
		memcpy(&count, object + field->count_offset, sizeof(count));
		return count == 0;
	}
	switch (field->type) {
	case KaSON_SCHEMA_TYPE_BOOL:
	case KaSON_SCHEMA_TYPE_INT: {
		int actual;
		memcpy(&actual, value, sizeof(actual));
		return actual == (int)field->policy.signed_value;
	}
	case KaSON_SCHEMA_TYPE_INT64: {
		int64_t actual;
		memcpy(&actual, value, sizeof(actual));
		return actual == field->policy.signed_value;
	}
	case KaSON_SCHEMA_TYPE_U32: {
		uint32_t actual;
		memcpy(&actual, value, sizeof(actual));
		return actual == (uint32_t)field->policy.unsigned_value;
	}
	case KaSON_SCHEMA_TYPE_U64: {
		uint64_t actual;
		memcpy(&actual, value, sizeof(actual));
		return actual == field->policy.unsigned_value;
	}
	case KaSON_SCHEMA_TYPE_DOUBLE: {
		double actual;
		memcpy(&actual, value, sizeof(actual));
		return actual == field->policy.real_value;
	}
	case KaSON_SCHEMA_TYPE_STRING: {
		size_t length = kason_schema_bounded_string_length((const char*)value,
			field->size);
		return length == field->policy.value_size && length != SIZE_MAX &&
			memcmp(value, field->policy.pointer_value, length) == 0;
	}
	case KaSON_SCHEMA_TYPE_STRING_U16: {
		size_t length = kason_schema_bounded_u16_length((const uint16_t*)value,
			field->size / sizeof(uint16_t));
		return length == field->policy.value_size && length != SIZE_MAX &&
			memcmp(value, field->policy.pointer_value,
				length * sizeof(uint16_t)) == 0;
	}
	case KaSON_SCHEMA_TYPE_STRING_U32: {
		size_t length = kason_schema_bounded_u32_length((const uint32_t*)value,
			field->size / sizeof(uint32_t));
		return length == field->policy.value_size && length != SIZE_MAX &&
			memcmp(value, field->policy.pointer_value,
				length * sizeof(uint32_t)) == 0;
	}
	case KaSON_SCHEMA_TYPE_STRUCT:
		return kason_schema_objects_equal(field->child_schema, value,
			(const unsigned char*)field->policy.pointer_value, depth + 1);
	default:
		return 0;
	}
}

static int kason_schema_pack_scalar(kason_writer* writer,
		const kason_schema_field* field, const unsigned char* value,
		size_t capacity, kason_schema_error* error, int array_index, int depth) {
	switch (field->type) {
	case KaSON_SCHEMA_TYPE_BOOL: {
		int actual;
		memcpy(&actual, value, sizeof(actual));
		if (actual != 0 && actual != 1) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_TYPE,
				field, array_index, depth);
		}
		return kason_writer_write(writer, actual ? "true" : "false",
			actual ? 4 : 5);
	}
	case KaSON_SCHEMA_TYPE_INT: {
		int actual;
		memcpy(&actual, value, sizeof(actual));
		return kason_writer_signed(writer, actual);
	}
	case KaSON_SCHEMA_TYPE_INT64: {
		int64_t actual;
		memcpy(&actual, value, sizeof(actual));
		return kason_writer_signed(writer, actual);
	}
	case KaSON_SCHEMA_TYPE_U32: {
		uint32_t actual;
		memcpy(&actual, value, sizeof(actual));
		return kason_writer_unsigned(writer, actual);
	}
	case KaSON_SCHEMA_TYPE_U64: {
		uint64_t actual;
		memcpy(&actual, value, sizeof(actual));
		return kason_writer_unsigned(writer, actual);
	}
	case KaSON_SCHEMA_TYPE_DOUBLE: {
		double actual;
		memcpy(&actual, value, sizeof(actual));
		return kason_writer_double(writer, actual);
	}
	case KaSON_SCHEMA_TYPE_STRING: {
		size_t length = kason_schema_bounded_string_length((const char*)value, capacity);
		if (length == SIZE_MAX) {
			return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_STRING_CAPACITY,
				field, array_index, depth);
		}
		if (kason_writer_json_string(writer, (const char*)value, length) !=
				KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, writer->status,
				field, array_index, depth);
		}
		return KaSON_SCHEMA_SUCCESS;
	}
	case KaSON_SCHEMA_TYPE_STRING_U16:
		if (kason_writer_json_string_u16(writer, (const uint16_t*)value,
				capacity / sizeof(uint16_t)) != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, writer->status,
				field, array_index, depth);
		}
		return KaSON_SCHEMA_SUCCESS;
	case KaSON_SCHEMA_TYPE_STRING_U32:
		if (kason_writer_json_string_u32(writer, (const uint32_t*)value,
				capacity / sizeof(uint32_t)) != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, writer->status,
				field, array_index, depth);
		}
		return KaSON_SCHEMA_SUCCESS;
	default:
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_INVALID_SCHEMA,
			field, array_index, depth);
	}
}

static int kason_schema_pack_array(kason_writer* writer,
		const kason_schema_field* field, const unsigned char* object,
		unsigned flags, kason_schema_error* error, int depth) {
	size_t count;
	size_t i;

	memcpy(&count, object + field->count_offset, sizeof(count));
	if (count > field->capacity) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_ARRAY_CAPACITY,
			field, (int)count, depth);
	}
	if (kason_writer_char(writer, '[') != KaSON_SCHEMA_SUCCESS) {
		return kason_schema_set_error(error, writer->status, field, -1, depth);
	}
	for (i = 0; i < count; i++) {
		const unsigned char* value = object + field->offset + i * field->element_size;
		int result;
		if (i > 0 && kason_writer_char(writer, ',') != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, writer->status, field, (int)i, depth);
		}
		if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
			result = kason_schema_pack_object(writer, field->child_schema, value,
				flags, error, depth + 1);
		} else {
			result = kason_schema_pack_scalar(writer, field, value,
				field->element_size, error, (int)i, depth);
		}
		if (result != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, result, field, (int)i, depth);
		}
	}
	if (kason_writer_char(writer, ']') != KaSON_SCHEMA_SUCCESS) {
		return kason_schema_set_error(error, writer->status, field, -1, depth);
	}
	return KaSON_SCHEMA_SUCCESS;
}

static int kason_schema_pack_object(kason_writer* writer,
		const kason_schema* schema, const unsigned char* object,
		unsigned flags, kason_schema_error* error, int depth) {
	int emitted = 0;
	int i;

	if (depth >= KaSON_MAX_NESTING) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_NESTING,
			NULL, -1, depth);
	}
	if (kason_writer_char(writer, '{') != KaSON_SCHEMA_SUCCESS) {
		return kason_schema_set_error(error, writer->status, NULL, -1, depth);
	}
	for (i = 0; i < schema->field_count; i++) {
		const kason_schema_field* field = &schema->fields[i];
		const unsigned char* value = object + field->offset;
		int result;

		if ((flags & KaSON_PACK_OMIT_DEFAULTS) != 0 &&
				kason_schema_field_is_default(field, object, depth)) {
			continue;
		}
		if (emitted && kason_writer_char(writer, ',') != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, writer->status, field, -1, depth);
		}
		if (kason_writer_json_string(writer, field->json_key,
				(size_t)field->key_length) != KaSON_SCHEMA_SUCCESS ||
				kason_writer_char(writer, ':') != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, writer->status, field, -1, depth);
		}
		if ((field->flags & KaSON_SCHEMA_FIELD_ARRAY) != 0) {
			result = kason_schema_pack_array(writer, field, object,
				flags, error, depth);
		} else if (field->type == KaSON_SCHEMA_TYPE_STRUCT) {
			result = kason_schema_pack_object(writer, field->child_schema,
				value, flags, error, depth + 1);
		} else {
			result = kason_schema_pack_scalar(writer, field, value, field->size,
				error, -1, depth);
		}
		if (result != KaSON_SCHEMA_SUCCESS) {
			return kason_schema_set_error(error, result, field, -1, depth);
		}
		emitted = 1;
	}
	if (kason_writer_char(writer, '}') != KaSON_SCHEMA_SUCCESS) {
		return kason_schema_set_error(error, writer->status, NULL, -1, depth);
	}
	return KaSON_SCHEMA_SUCCESS;
}

int kason_pack(kason_writer* writer, const kason_schema* schema,
		const void* object, unsigned flags, kason_schema_error* error) {
	int result;

	kason_schema_clear_error(error);
	if (writer == NULL || schema == NULL || !schema->initialized || object == NULL ||
			(flags & ~KaSON_PACK_OMIT_DEFAULTS) != 0) {
		return kason_schema_set_error(error, KaSON_SCHEMA_ERROR_INVALID_SCHEMA,
			NULL, -1, 0);
	}
	kason_writer_reset(writer);
	result = kason_schema_pack_object(writer, schema,
		(const unsigned char*)object, flags, error, 0);
	if (result == KaSON_SCHEMA_SUCCESS &&
			kason_writer_flush(writer) != KaSON_SCHEMA_SUCCESS) {
		result = kason_schema_set_error(error, writer->status, NULL, -1, 0);
	}
	return result;
}
