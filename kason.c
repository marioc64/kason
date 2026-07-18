
#define STDIO_NULL

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef STDIO_NULL
#include <stdio.h>
#else
#define NULL (0)
#endif

#include "kason_internal.h"

#define KaSON_CHAR_OBJECT_START      ('{')
#define KaSON_CHAR_OBJECT_END        ('}')
#define KaSON_CHAR_ARRAY_START       ('[')
#define KaSON_CHAR_ARRAY_END         (']')
#define KaSON_CHAR_PARAM_SEPARATOR   (',')
#define KaSON_CHAR_KEY_VAL_SEPARATOR (':')
#define KaSON_CHAR_STRING_SEPARATOR  ('"')
#define KaSON_CHAR_STRING_FMT_ESCAPE ('\\')

#define KaSON_STATE_FIND_OBJECT_BEGIN         (  1 ) // initial state
#define KaSON_STATE_FIND_OBJECT_END           (  2 )
#define KaSON_STATE_FIND_KEY_BEGIN            (  3 )
#define KaSON_STATE_FIND_KEY_END              (  4 )
#define KaSON_STATE_FIND_VALUE_SEPARATOR      (  5 )
#define KaSON_STATE_FIND_VALUE_BEGIN          (  6 )
#define KaSON_STATE_FIND_GENERIC_VALUE_END    (  7 )
#define KaSON_STATE_FIND_STRING_VALUE_END     (  8 )
#define KaSON_STATE_FIND_OBJECT_VALUE_END     (  9 )
#define KaSON_STATE_FIND_OBJECT_VALUE_END_STR ( 10 )
#define KaSON_STATE_FIND_ARRAY_VALUE_END      ( 11 )
#define KaSON_STATE_FIND_ARRAY_VALUE_END_STR  ( 12 )
#define KaSON_STATE_FIND_PARAM_SEPARATOR      ( 13 )
#define KaSON_STATE_KEY_FORMAT_CHAR           ( 14 )
#define KaSON_STATE_STRING_VALUE_FORMAT_CHAR  ( 15 )
#define KaSON_STATE_ASTRING_VALUE_FORMAT_CHAR ( 16 )
#define KaSON_STATE_OSTRING_VALUE_FORMAT_CHAR ( 17 )
#define KaSON_STATE_FIND_NUMBER_VALUE_END     ( 18 )

#define KaSON_STATE_COMPLETE                  ( 50 )

#define KaSON_STATE_ERR_INCORRECT_CHAR        ( -1 )
#define KaSON_STATE_ERR_INCORRECT_STATE       ( -2 )
#define KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR ( -3 )

#define KaSON_STREAM_STATE_FIND_ROOT          (  1 )
#define KaSON_STREAM_STATE_FIND_KEY_BEGIN     (  2 )
#define KaSON_STREAM_STATE_FIND_KEY_END       (  3 )
#define KaSON_STREAM_STATE_FIND_VALUE_SEP     (  4 )
#define KaSON_STREAM_STATE_FIND_VALUE_BEGIN   (  5 )
#define KaSON_STREAM_STATE_FIND_STRING_END    (  6 )
#define KaSON_STREAM_STATE_FIND_GENERIC_END   (  7 )
#define KaSON_STREAM_STATE_FIND_PARAM_SEP     (  8 )
#define KaSON_STREAM_STATE_STREAM_CONTAINER   (  9 )
#define KaSON_STREAM_STATE_KEY_ESCAPE         ( 10 )
#define KaSON_STREAM_STATE_STRING_ESCAPE      ( 11 )

#define KaSON_STREAM_STATE_COMPLETE           ( 50 )

#define KaSON_SURROGATE_NONE             ( 0 )
#define KaSON_SURROGATE_EXPECT_BACKSLASH ( 1 )
#define KaSON_SURROGATE_EXPECT_U         ( 2 )
#define KaSON_SURROGATE_LOW_HEX          ( 3 )

//#define WITH_LOG

#if defined(WITH_LOG)
#define LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define LOG(...)
#endif

#define BOOL  int
#define TRUE  (1)
#define FALSE (0)

#if defined(__GNUC__) || defined(__clang__)
#define KaSON_HOT_ALIGN __attribute__((aligned(64)))
#else
#define KaSON_HOT_ALIGN
#endif

static BOOL is_digit(char c) {
	return c >= '0' && c<='9';
}

static BOOL is_hex_digit(char c) {
	return is_digit(c) ||
		(c >= 'a' && c <= 'f') ||
		(c >= 'A' && c <= 'F');
}

static BOOL is_control_char(char c) {
	return ((unsigned char)c) < 0x20;
}

static BOOL is_whitespace(char c) {
	return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

static char* kason_skip_plain_string(char* ptr, char* end) {
	char* next = ptr + 1;

	while ((end ? next <= end : *next != 0) &&
			(unsigned char)*next >= 0x20 && (unsigned char)*next < 0x80 &&
			*next != KaSON_CHAR_STRING_SEPARATOR &&
			*next != KaSON_CHAR_STRING_FMT_ESCAPE) {
		ptr = next++;
	}
	return ptr;
}

static char* kason_skip_number_digits(char* ptr, char* end) {
	char* next = ptr + 1;

	while ((end ? next < end : *next != 0 && *(next + 1) != 0) &&
			is_digit(*next)) {
		ptr = next++;
	}
	return ptr;
}

static char* kason_skip_keyword(char* ptr, char* end) {
	const char* keyword;
	int length;
	int i;

	if (*ptr == 't') {
		keyword = "true";
		length = 4;
	} else if (*ptr == 'f') {
		keyword = "false";
		length = 5;
	} else if (*ptr == 'n') {
		keyword = "null";
		length = 4;
	} else {
		return ptr;
	}
	if (end && end - ptr < length - 1) {
		return ptr;
	}
	for (i = 1; i < length; i++) {
		if ((!end && ptr[i] == 0) || ptr[i] != keyword[i]) {
			return ptr;
		}
	}
	if ((end && ptr + length - 1 == end) ||
			(!end && ptr[length] == 0)) {
		return ptr + length - 2;
	}
	return ptr + length - 1;
}

static BOOL is_simple_escape(char c) {
	return c == '"' ||
		c == '\\' ||
		c == '/' ||
		c == 'b' ||
		c == 'f' ||
		c == 'n' ||
		c == 'r' ||
		c == 't';
}

#define KaSON_STRING_NEXT_ERROR ( -1 )
#define KaSON_STRING_NEXT_END   (  0 )
#define KaSON_STRING_NEXT_VALUE (  1 )

static int kason_hex_value(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static int kason_decode_hex4(const char* ptr, uint32_t* value) {
	uint32_t result = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int digit = kason_hex_value(ptr[i]);
		if (digit < 0) {
			return FALSE;
		}
		result = (result << 4) | (uint32_t)digit;
	}
	*value = result;
	return TRUE;
}

/*
 * Reads one decoded Unicode scalar value from an inclusive JSON string slice.
 * The slice excludes its surrounding quotes but still contains JSON escapes.
 */
static int kason_string_next_internal(const char** cursor, const char* end, uint32_t* value, BOOL strict_json) {
	const char* ptr;
	unsigned char first;
	uint32_t scalar;
	uint32_t minimum;
	int available;
	int continuation_count;
	int i;

	if (NULL == cursor || NULL == *cursor || NULL == end || NULL == value) {
		return KaSON_STRING_NEXT_ERROR;
	}
	ptr = *cursor;
	if (ptr > end) {
		return KaSON_STRING_NEXT_END;
	}
	available = (int)(end - ptr + 1);
	first = (unsigned char)ptr[0];

	if (first == (unsigned char)KaSON_CHAR_STRING_FMT_ESCAPE) {
		if (available < 2) {
			return KaSON_STRING_NEXT_ERROR;
		}
		switch (ptr[1]) {
		case '"': scalar = '"'; break;
		case '\\': scalar = '\\'; break;
		case '/': scalar = '/'; break;
		case 'b': scalar = '\b'; break;
		case 'f': scalar = '\f'; break;
		case 'n': scalar = '\n'; break;
		case 'r': scalar = '\r'; break;
		case 't': scalar = '\t'; break;
		case 'u':
			if (available < 6 || !kason_decode_hex4(ptr + 2, &scalar)) {
				return KaSON_STRING_NEXT_ERROR;
			}
			if (scalar >= 0xd800 && scalar <= 0xdbff) {
				uint32_t low;
				if (available < 12 || ptr[6] != '\\' || ptr[7] != 'u' ||
						!kason_decode_hex4(ptr + 8, &low) || low < 0xdc00 || low > 0xdfff) {
					return KaSON_STRING_NEXT_ERROR;
				}
				scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
				*cursor = ptr + 12;
				*value = scalar;
				return KaSON_STRING_NEXT_VALUE;
			}
			if (scalar >= 0xdc00 && scalar <= 0xdfff) {
				return KaSON_STRING_NEXT_ERROR;
			}
			*cursor = ptr + 6;
			*value = scalar;
			return KaSON_STRING_NEXT_VALUE;
		default:
			return KaSON_STRING_NEXT_ERROR;
		}
		*cursor = ptr + 2;
		*value = scalar;
		return KaSON_STRING_NEXT_VALUE;
	}

	if (first < 0x80) {
		if (strict_json && (first < 0x20 || first == (unsigned char)KaSON_CHAR_STRING_SEPARATOR)) {
			return KaSON_STRING_NEXT_ERROR;
		}
		*cursor = ptr + 1;
		*value = first;
		return KaSON_STRING_NEXT_VALUE;
	}

	if (first >= 0xc2 && first <= 0xdf) {
		continuation_count = 1;
		scalar = first & 0x1f;
		minimum = 0x80;
	} else if (first >= 0xe0 && first <= 0xef) {
		continuation_count = 2;
		scalar = first & 0x0f;
		minimum = 0x800;
	} else if (first >= 0xf0 && first <= 0xf4) {
		continuation_count = 3;
		scalar = first & 0x07;
		minimum = 0x10000;
	} else {
		return KaSON_STRING_NEXT_ERROR;
	}
	if (available < continuation_count + 1) {
		return KaSON_STRING_NEXT_ERROR;
	}
	for (i = 1; i <= continuation_count; i++) {
		unsigned char continuation = (unsigned char)ptr[i];
		if ((continuation & 0xc0) != 0x80) {
			return KaSON_STRING_NEXT_ERROR;
		}
		scalar = (scalar << 6) | (continuation & 0x3f);
	}
	if (scalar < minimum || scalar > 0x10ffff ||
			(scalar >= 0xd800 && scalar <= 0xdfff)) {
		return KaSON_STRING_NEXT_ERROR;
	}
	*cursor = ptr + continuation_count + 1;
	*value = scalar;
	return KaSON_STRING_NEXT_VALUE;
}

static int kason_string_next(const char** cursor, const char* end, uint32_t* value) {
	return kason_string_next_internal(cursor, end, value, TRUE);
}

static BOOL kason_string_is_valid(const char* begin, const char* end) {
	const char* ptr = begin;
	uint32_t value;
	int result;

	if (NULL == begin || NULL == end) {
		return FALSE;
	}
	while ((result = kason_string_next(&ptr, end, &value)) == KaSON_STRING_NEXT_VALUE) {
	}
	return result == KaSON_STRING_NEXT_END;
}

static int kason_utf8_encode(uint32_t value, char output[4]) {
	if (value <= 0x7f) {
		output[0] = (char)value;
		return 1;
	}
	if (value <= 0x7ff) {
		output[0] = (char)(0xc0 | (value >> 6));
		output[1] = (char)(0x80 | (value & 0x3f));
		return 2;
	}
	if (value <= 0xffff) {
		output[0] = (char)(0xe0 | (value >> 12));
		output[1] = (char)(0x80 | ((value >> 6) & 0x3f));
		output[2] = (char)(0x80 | (value & 0x3f));
		return 3;
	}
	output[0] = (char)(0xf0 | (value >> 18));
	output[1] = (char)(0x80 | ((value >> 12) & 0x3f));
	output[2] = (char)(0x80 | ((value >> 6) & 0x3f));
	output[3] = (char)(0x80 | (value & 0x3f));
	return 4;
}

typedef struct s_kason_hash_sample {
	unsigned char first[4];
	unsigned char last[4];
	int length;
} kason_hash_sample;

static uint32_t kason_pack_hash_bytes(const unsigned char* value, int length) {
	uint32_t packed = 0;
	int i;

	for (i = 0; i < length; i++) {
		packed |= (uint32_t)value[i] << (i * 8);
	}
	return packed;
}

static uint32_t kason_pack_hash_four(const unsigned char* value) {
	return (uint32_t)value[0] |
		((uint32_t)value[1] << 8) |
		((uint32_t)value[2] << 16) |
		((uint32_t)value[3] << 24);
}

static uint32_t kason_hash_parts(uint32_t first, uint32_t last, int length) {
	uint32_t hash = (uint32_t)length * UINT32_C(0x9e3779b1);

	hash ^= first * UINT32_C(0x85ebca6b);
	hash = (hash << 13) | (hash >> 19);
	hash ^= last * UINT32_C(0xc2b2ae35);
	return hash ^ (hash >> 16);
}

static uint32_t kason_hash_bytes(const char* value, int length) {
	uint32_t first;
	uint32_t last;

	if (length >= 4) {
		first = kason_pack_hash_four((const unsigned char*)value);
		last = kason_pack_hash_four((const unsigned char*)value + length - 4);
	} else {
		first = kason_pack_hash_bytes((const unsigned char*)value, length);
		last = first;
	}

	return kason_hash_parts(first, last, length);
}

static void kason_hash_sample_add(kason_hash_sample* sample, unsigned char value) {
	if (sample->length < 4) {
		sample->first[sample->length] = value;
	}
	sample->last[sample->length & 3] = value;
	sample->length++;
}

static uint32_t kason_hash_sample_finish(const kason_hash_sample* sample) {
	unsigned char last[4] = {0, 0, 0, 0};
	int sample_length = sample->length < 4 ? sample->length : 4;
	int first_index = sample->length - sample_length;
	int i;

	for (i = 0; i < sample_length; i++) {
		last[i] = sample->last[(first_index + i) & 3];
	}
	return kason_hash_parts(kason_pack_hash_bytes(sample->first, sample_length),
			kason_pack_hash_bytes(last, sample_length), sample->length);
}

static int kason_decoded_key_info(const kason_key* key, uint32_t* hash, int* length) {
	const char* ptr = key->begin;
	uint32_t value;
	kason_hash_sample sample = {{0, 0, 0, 0}, {0, 0, 0, 0}, 0};
	int result;

	while ((result = kason_string_next(&ptr, key->end, &value)) == KaSON_STRING_NEXT_VALUE) {
		char encoded[4];
		int encoded_length = kason_utf8_encode(value, encoded);
		int i;

		for (i = 0; i < encoded_length; i++) {
			kason_hash_sample_add(&sample, (unsigned char)encoded[i]);
		}
	}
	if (result != KaSON_STRING_NEXT_END) {
		return FALSE;
	}
	*hash = kason_hash_sample_finish(&sample);
	*length = sample.length;
	return TRUE;
}

static BOOL kason_decoded_key_equals(const kason_key* key,
		const kason_lookup_key* expected) {
	const char* ptr = key->begin;
	uint32_t value;
	int offset = 0;
	int result;
	BOOL plain_ascii = TRUE;

	for (ptr = key->begin; ptr <= key->end; ptr++) {
		if (*ptr == KaSON_CHAR_STRING_FMT_ESCAPE || (unsigned char)*ptr >= 0x80) {
			plain_ascii = FALSE;
			break;
		}
	}
	if (plain_ascii) {
		int length = key->end >= key->begin ? (int)(key->end - key->begin + 1) : 0;
		return length == expected->length &&
			(length == 0 || memcmp(key->begin, expected->value, (size_t)length) == 0);
	}
	ptr = key->begin;

	while ((result = kason_string_next(&ptr, key->end, &value)) == KaSON_STRING_NEXT_VALUE) {
		char encoded[4];
		int encoded_length = kason_utf8_encode(value, encoded);
		int i;

		if (encoded_length > expected->length - offset) {
			return FALSE;
		}
		for (i = 0; i < encoded_length; i++) {
			if (encoded[i] != expected->value[offset++]) {
				return FALSE;
			}
		}
	}
	return result == KaSON_STRING_NEXT_END && offset == expected->length;
}

static BOOL kason_slice_equals(const char* begin, const char* end, const char* expected) {
	const char* ptr = begin;
	const char* expected_ptr = expected;

	if (NULL == begin || NULL == end || begin > end) {
		return FALSE;
	}
	for (; ptr <= end && *expected_ptr != 0; ptr++, expected_ptr++) {
		if (*ptr != *expected_ptr) {
			return FALSE;
		}
	}
	return ptr > end && *expected_ptr == 0;
}

static BOOL is_valid_number(const char* begin, const char* end) {
	const char* ptr = begin;

	if (NULL == begin || NULL == end || begin > end) {
		return FALSE;
	}
	if (*ptr == '-') {
		ptr++;
		if (ptr > end) {
			return FALSE;
		}
	}
	if (*ptr == '0') {
		ptr++;
	} else if (*ptr >= '1' && *ptr <= '9') {
		for (ptr++; ptr <= end && is_digit(*ptr); ptr++) {
		}
	} else {
		return FALSE;
	}
	if (ptr <= end && *ptr == '.') {
		ptr++;
		if (ptr > end || !is_digit(*ptr)) {
			return FALSE;
		}
		for (ptr++; ptr <= end && is_digit(*ptr); ptr++) {
		}
	}
	if (ptr <= end && (*ptr == 'e' || *ptr == 'E')) {
		ptr++;
		if (ptr <= end && (*ptr == '+' || *ptr == '-')) {
			ptr++;
		}
		if (ptr > end || !is_digit(*ptr)) {
			return FALSE;
		}
		for (ptr++; ptr <= end && is_digit(*ptr); ptr++) {
		}
	}
	return ptr > end;
}

typedef enum {
	KaSON_NUMBER_INVALID = -1,
	KaSON_NUMBER_SIGN,
	KaSON_NUMBER_ZERO,
	KaSON_NUMBER_INTEGER,
	KaSON_NUMBER_DECIMAL_POINT,
	KaSON_NUMBER_FRACTION,
	KaSON_NUMBER_EXPONENT_MARK,
	KaSON_NUMBER_EXPONENT_SIGN,
	KaSON_NUMBER_EXPONENT
} kason_number_state;

static kason_number_state kason_number_begin(char c) {
	if (c == '-') {
		return KaSON_NUMBER_SIGN;
	}
	if (c == '0') {
		return KaSON_NUMBER_ZERO;
	}
	if (c >= '1' && c <= '9') {
		return KaSON_NUMBER_INTEGER;
	}
	return KaSON_NUMBER_INVALID;
}

static kason_number_state kason_number_next(kason_number_state state, char c) {
	switch (state) {
	case KaSON_NUMBER_SIGN:
		return c == '0' ? KaSON_NUMBER_ZERO
			: (c >= '1' && c <= '9' ? KaSON_NUMBER_INTEGER : KaSON_NUMBER_INVALID);
	case KaSON_NUMBER_ZERO:
		return c == '.' ? KaSON_NUMBER_DECIMAL_POINT
			: (c == 'e' || c == 'E' ? KaSON_NUMBER_EXPONENT_MARK : KaSON_NUMBER_INVALID);
	case KaSON_NUMBER_INTEGER:
		if (is_digit(c)) {
			return KaSON_NUMBER_INTEGER;
		}
		return c == '.' ? KaSON_NUMBER_DECIMAL_POINT
			: (c == 'e' || c == 'E' ? KaSON_NUMBER_EXPONENT_MARK : KaSON_NUMBER_INVALID);
	case KaSON_NUMBER_DECIMAL_POINT:
		return is_digit(c) ? KaSON_NUMBER_FRACTION : KaSON_NUMBER_INVALID;
	case KaSON_NUMBER_FRACTION:
		if (is_digit(c)) {
			return KaSON_NUMBER_FRACTION;
		}
		return c == 'e' || c == 'E' ? KaSON_NUMBER_EXPONENT_MARK : KaSON_NUMBER_INVALID;
	case KaSON_NUMBER_EXPONENT_MARK:
		if (c == '+' || c == '-') {
			return KaSON_NUMBER_EXPONENT_SIGN;
		}
		return is_digit(c) ? KaSON_NUMBER_EXPONENT : KaSON_NUMBER_INVALID;
	case KaSON_NUMBER_EXPONENT_SIGN:
		return is_digit(c) ? KaSON_NUMBER_EXPONENT : KaSON_NUMBER_INVALID;
	case KaSON_NUMBER_EXPONENT:
		return is_digit(c) ? KaSON_NUMBER_EXPONENT : KaSON_NUMBER_INVALID;
	case KaSON_NUMBER_INVALID:
	default:
		return KaSON_NUMBER_INVALID;
	}
}

static BOOL kason_number_complete(kason_number_state state) {
	return state == KaSON_NUMBER_ZERO || state == KaSON_NUMBER_INTEGER ||
		state == KaSON_NUMBER_FRACTION || state == KaSON_NUMBER_EXPONENT;
}

typedef struct s_kason_number_accumulator {
	int kind;
	int negative;
	int integer_form;
	int integer_overflow;
	uint64_t magnitude;
	uint64_t significand;
	long long fraction_digits;
	long long dropped_digits;
	long long explicit_exponent;
	int significant_digits;
	int exponent_negative;
	int nonzero;
	int round_up;
} kason_number_accumulator;

static void kason_number_accumulator_init(kason_number_accumulator* accumulator,
		int kind) {
	memset(accumulator, 0, sizeof(*accumulator));
	accumulator->kind = kind;
	accumulator->integer_form = TRUE;
}

static void kason_number_accumulator_double_digit(
		kason_number_accumulator* accumulator, unsigned int digit) {
	if (!accumulator->nonzero && digit == 0) {
		return;
	}
	accumulator->nonzero = TRUE;
	if (accumulator->significant_digits < 19) {
		accumulator->significand = accumulator->significand * 10U + digit;
		accumulator->significant_digits++;
	} else {
		if (accumulator->dropped_digits == 0 && digit >= 5) {
			accumulator->round_up = TRUE;
		}
		if (accumulator->dropped_digits < 1000000) {
			accumulator->dropped_digits++;
		}
	}
}

static void kason_number_accumulator_feed(kason_number_accumulator* accumulator,
		char c, kason_number_state state) {
	unsigned int digit;

	if (accumulator->kind == KaSON_CACHED_NUMBER_NONE) {
		return;
	}
	if (state == KaSON_NUMBER_SIGN) {
		accumulator->negative = TRUE;
		return;
	}
	if (state == KaSON_NUMBER_DECIMAL_POINT || state == KaSON_NUMBER_FRACTION ||
			state == KaSON_NUMBER_EXPONENT_MARK ||
			state == KaSON_NUMBER_EXPONENT_SIGN ||
			state == KaSON_NUMBER_EXPONENT) {
		accumulator->integer_form = FALSE;
	}
	if (state == KaSON_NUMBER_EXPONENT_SIGN) {
		accumulator->exponent_negative = c == '-';
		return;
	}
	if (!is_digit(c)) {
		return;
	}
	digit = (unsigned int)(c - '0');
	if (state == KaSON_NUMBER_ZERO || state == KaSON_NUMBER_INTEGER) {
		if (accumulator->kind == KaSON_CACHED_NUMBER_SIGNED ||
				accumulator->kind == KaSON_CACHED_NUMBER_UNSIGNED) {
			if (accumulator->magnitude > (UINT64_MAX - digit) / 10U) {
				accumulator->integer_overflow = TRUE;
			} else {
				accumulator->magnitude = accumulator->magnitude * 10U + digit;
			}
		} else if (accumulator->kind == KaSON_CACHED_NUMBER_DOUBLE) {
			kason_number_accumulator_double_digit(accumulator, digit);
		}
	} else if (state == KaSON_NUMBER_FRACTION &&
			accumulator->kind == KaSON_CACHED_NUMBER_DOUBLE) {
		if (accumulator->fraction_digits < 1000000) {
			accumulator->fraction_digits++;
		}
		kason_number_accumulator_double_digit(accumulator, digit);
	} else if (state == KaSON_NUMBER_EXPONENT &&
			accumulator->kind == KaSON_CACHED_NUMBER_DOUBLE &&
			accumulator->explicit_exponent < 1000000) {
		accumulator->explicit_exponent =
			accumulator->explicit_exponent * 10 + (long long)digit;
		if (accumulator->explicit_exponent > 1000000) {
			accumulator->explicit_exponent = 1000000;
		}
	}
}

static void kason_number_accumulator_finish(
		kason_number_accumulator* accumulator, kason_cached_number* cached) {
	uint64_t limit;
	long long decimal_exponent;
	char normalized[64];
	char* parsed_end;
	int length;

	cached->kind = accumulator->kind;
	cached->result = KaSON_CONVERT_SUCCESS;
	if (accumulator->kind == KaSON_CACHED_NUMBER_SIGNED ||
			accumulator->kind == KaSON_CACHED_NUMBER_UNSIGNED) {
		if (accumulator->integer_overflow) {
			cached->result = KaSON_CONVERT_RANGE;
			return;
		}
		if (!accumulator->integer_form) {
			cached->result = KaSON_CONVERT_ERROR;
			return;
		}
		if (accumulator->kind == KaSON_CACHED_NUMBER_UNSIGNED) {
			if (accumulator->negative) {
				cached->result = KaSON_CONVERT_RANGE;
				return;
			}
			cached->value.unsigned_value = accumulator->magnitude;
			return;
		}
		limit = accumulator->negative
			? (uint64_t)INT64_MAX + 1U
			: (uint64_t)INT64_MAX;
		if (accumulator->magnitude > limit) {
			cached->result = KaSON_CONVERT_RANGE;
			return;
		}
		cached->value.signed_value = accumulator->negative
			? (accumulator->magnitude == (uint64_t)INT64_MAX + 1U
				? INT64_MIN : -(int64_t)accumulator->magnitude)
			: (int64_t)accumulator->magnitude;
		return;
	}
	if (accumulator->kind != KaSON_CACHED_NUMBER_DOUBLE) {
		return;
	}
	if (!accumulator->nonzero) {
		cached->value.real_value = accumulator->negative ? -0.0 : 0.0;
		return;
	}
	if (accumulator->round_up) {
		accumulator->significand++;
	}
	if (accumulator->exponent_negative) {
		accumulator->explicit_exponent = -accumulator->explicit_exponent;
	}
	decimal_exponent = accumulator->explicit_exponent -
		accumulator->fraction_digits + accumulator->dropped_digits;
	length = snprintf(normalized, sizeof(normalized), "%s%llue%lld",
		accumulator->negative ? "-" : "",
		(unsigned long long)accumulator->significand, decimal_exponent);
	if (length < 0 || length >= (int)sizeof(normalized)) {
		cached->result = KaSON_CONVERT_ERROR;
		return;
	}
	errno = 0;
	cached->value.real_value = strtod(normalized, &parsed_end);
	if (errno == ERANGE) {
		cached->result = KaSON_CONVERT_RANGE;
	} else if (parsed_end != normalized + length) {
		cached->result = KaSON_CONVERT_ERROR;
	}
}

/*
 * Checks begin of key or value
 */
static BOOL check_begin(const char c, int* state) {
	if (*state != KaSON_STATE_FIND_KEY_BEGIN && *state!=KaSON_STATE_FIND_VALUE_BEGIN) {
		*state = KaSON_STATE_ERR_INCORRECT_STATE;
		return FALSE;
	}
	if (is_whitespace(c)) {
		return FALSE; // omit whitespace character
	}
	// non whitespace character
	if (*state == KaSON_STATE_FIND_KEY_BEGIN && c == KaSON_CHAR_STRING_SEPARATOR) {
		*state = KaSON_STATE_FIND_KEY_END;
		return TRUE;
	} else if (*state == KaSON_STATE_FIND_VALUE_BEGIN && c == KaSON_CHAR_STRING_SEPARATOR) {
		*state = KaSON_STATE_FIND_STRING_VALUE_END;
		return TRUE;
	} else if (*state == KaSON_STATE_FIND_VALUE_BEGIN && c == KaSON_CHAR_OBJECT_START) {
		*state = KaSON_STATE_FIND_OBJECT_VALUE_END;
		return TRUE;
	} else if (*state == KaSON_STATE_FIND_VALUE_BEGIN && c == KaSON_CHAR_ARRAY_START) {
		*state = KaSON_STATE_FIND_ARRAY_VALUE_END;
		return TRUE;
	} else if (*state == KaSON_STATE_FIND_VALUE_BEGIN) {
		*state = KaSON_STATE_FIND_GENERIC_VALUE_END;
		return TRUE;
	}
	*state = KaSON_STATE_ERR_INCORRECT_CHAR;
	return FALSE;
}

/*
 * Checks end of array or object. Open and close characters increments and decrements variable pointed by counter.
 * When counter becomes 0 (from 1) then end of object or array is reached and TRUE is returned.
 *! \param c - current character
 *! \param counter - pointer to counter value (should be 1 on first call)
 */
static BOOL check_end(char c, int* array_bracket_counter, int* object_bracket_counter, int* state) {
	if (KaSON_CHAR_ARRAY_START == c) {
		(*array_bracket_counter) ++;
	} else if (KaSON_CHAR_ARRAY_END == c) {
		(*array_bracket_counter) --;
		if (*array_bracket_counter < 0) {
			*state = KaSON_STATE_ERR_INCORRECT_CHAR;
			return FALSE;
		}
		if ( 0 == *array_bracket_counter && KaSON_STATE_FIND_ARRAY_VALUE_END == *state) {
			return TRUE;
		}
	} else if (KaSON_CHAR_OBJECT_START == c) {
		(*object_bracket_counter) ++;
	} else if (KaSON_CHAR_OBJECT_END == c) {
		(*object_bracket_counter) --;
		if (*object_bracket_counter < 0) {
			*state = KaSON_STATE_ERR_INCORRECT_CHAR;
			return FALSE;
		}
		if ( 0 == *object_bracket_counter && KaSON_STATE_FIND_OBJECT_VALUE_END== *state) {
			return TRUE;
		}
	} else if (KaSON_CHAR_STRING_SEPARATOR == c && KaSON_STATE_FIND_ARRAY_VALUE_END == *state) {
		*state = KaSON_STATE_FIND_ARRAY_VALUE_END_STR; // string mode
	} else if (KaSON_CHAR_STRING_SEPARATOR == c && KaSON_STATE_FIND_OBJECT_VALUE_END == *state) {
		*state = KaSON_STATE_FIND_OBJECT_VALUE_END_STR; // string mode
	}
	return FALSE;
}

static void restore_escaped_state(int* state) {
	switch(*state) {
	case KaSON_STATE_STRING_VALUE_FORMAT_CHAR:
		*state = KaSON_STATE_FIND_STRING_VALUE_END;
		break;
	case KaSON_STATE_KEY_FORMAT_CHAR:
		*state = KaSON_STATE_FIND_KEY_END;
		break;
	case KaSON_STATE_ASTRING_VALUE_FORMAT_CHAR:
		*state = KaSON_STATE_FIND_ARRAY_VALUE_END_STR;
		break;
	case KaSON_STATE_OSTRING_VALUE_FORMAT_CHAR:
		*state = KaSON_STATE_FIND_OBJECT_VALUE_END_STR;
		break;
	default:
		*state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
	}
}

typedef enum {M_AUTO, M_OBJECT, M_ARRAY, M_STRING, M_GENERIC} KaSON_PARSE_MODE;

typedef struct s_kason_lookup_context {
	const kason_lookup_table* table;
	kason_lookup_callback callback;
	kason_lookup_conversion_selector selector;
	kason_lookup_converted_callback converted_callback;
	void* user_data;
} kason_lookup_context;

static const kason_lookup_key* kason_lookup_find(
		const kason_lookup_table* table, const kason_key* key,
		uint32_t hash, int length) {
	int index = (int)(hash % (uint32_t)table->capacity);
	int probes;

	for (probes = 0; probes < table->capacity; probes++) {
		const kason_lookup_key* expected = &table->slots[index];
		if (expected->value == NULL) {
			return NULL;
		}
		if (hash == expected->hash && length == expected->length &&
				kason_decoded_key_equals(key, expected)) {
			return expected;
		}
		index++;
		if (index == table->capacity) {
			index = 0;
		}
	}
	return NULL;
}

static int kason_validate_callback(kason_key* key, kason_data* data, int count, void* user_data) {
	(void)key;
	(void)data;
	(void)count;
	(void)user_data;
	return KaSON_CALLBACK_CONTINUE;
}

static void kason_stream_clear_scratch(kason_stream* stream) {
	stream->scratch_length = stream->scratch_size > 0 ? 1 : 0;
	stream->key_begin = 0;
	stream->key_end = 0;
	stream->value_begin = 0;
	stream->value_end = 0;
	stream->direct_key_begin = NULL;
	stream->direct_key_end = NULL;
	stream->direct_value_begin = NULL;
	stream->direct_value_end = NULL;
	stream->has_key = FALSE;
	stream->escape_hex_left = 0;
}

static int kason_stream_append_scratch(kason_stream* stream, char c) {
	if (stream->scratch_length >= stream->scratch_size) {
		return KaSON_PARSE_RESULT_ERROR_BUFFER_FULL;
	}
	stream->scratch[stream->scratch_length++] = c;
	return KaSON_PARSE_RESULT_SUCCESS;
}

static char* kason_stream_slice_begin(kason_stream* stream, int begin) {
	return stream->scratch + begin;
}

static char* kason_stream_slice_end(kason_stream* stream, int begin, int end) {
	return end >= begin ? stream->scratch + end : stream->scratch + begin - 1;
}

static int kason_stream_copy_direct(kason_stream* stream, char** direct_begin,
		char** direct_end, int* scratch_begin, int* scratch_end) {
	char* ptr;
	int result;

	if (*direct_begin == NULL) {
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	*scratch_begin = stream->scratch_length;
	*scratch_end = stream->scratch_length - 1;
	for (ptr = *direct_begin; ptr <= *direct_end; ptr++) {
		result = kason_stream_append_scratch(stream, *ptr);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		*scratch_end = stream->scratch_length - 1;
	}
	*direct_begin = NULL;
	*direct_end = NULL;
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_stream_preserve_direct(kason_stream* stream) {
	int result = kason_stream_copy_direct(stream,
			&stream->direct_key_begin, &stream->direct_key_end,
			&stream->key_begin, &stream->key_end);

	if (result != KaSON_PARSE_RESULT_SUCCESS) {
		return result;
	}
	return kason_stream_copy_direct(stream,
			&stream->direct_value_begin, &stream->direct_value_end,
			&stream->value_begin, &stream->value_end);
}

static char* kason_stream_key_begin(kason_stream* stream) {
	return stream->direct_key_begin != NULL
		? stream->direct_key_begin
		: kason_stream_slice_begin(stream, stream->key_begin);
}

static char* kason_stream_key_end(kason_stream* stream) {
	return stream->direct_key_begin != NULL
		? stream->direct_key_end
		: kason_stream_slice_end(stream, stream->key_begin, stream->key_end);
}

static char* kason_stream_value_begin(kason_stream* stream) {
	return stream->direct_value_begin != NULL
		? stream->direct_value_begin
		: kason_stream_slice_begin(stream, stream->value_begin);
}

static char* kason_stream_value_end(kason_stream* stream) {
	return stream->direct_value_begin != NULL
		? stream->direct_value_end
		: kason_stream_slice_end(stream, stream->value_begin, stream->value_end);
}

static kason_key* kason_stream_make_key(kason_stream* stream, kason_key* key) {
	if (!stream->has_key) {
		return NULL;
	}
	key->begin = kason_stream_key_begin(stream);
	key->end = kason_stream_key_end(stream);
	return key;
}

static int kason_stream_emit(kason_stream* stream, int type, int event, char* begin, char* end, BOOL include_key) {
	kason_key key;
	kason_stream_data data = {type, event, begin, end};
	kason_key* key_ptr = include_key ? kason_stream_make_key(stream, &key) : NULL;

	if (stream->callback(key_ptr, &data, stream->user_data) == KaSON_CALLBACK_BREAK) {
		stream->callback_break = TRUE;
		stream->complete = TRUE;
		stream->state = KaSON_STREAM_STATE_COMPLETE;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_stream_process_escape(kason_stream* stream, char c, int restore_state) {
	int result;

	if (stream->escape_hex_left > 0) {
		if (!is_hex_digit(c)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		result = kason_stream_append_scratch(stream, c);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		stream->escape_hex_left--;
		if (stream->escape_hex_left == 0) {
			stream->state = restore_state;
		}
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (c == 'u') {
		result = kason_stream_append_scratch(stream, c);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		stream->escape_hex_left = 4;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (is_simple_escape(c)) {
		result = kason_stream_append_scratch(stream, c);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		stream->state = restore_state;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	return KaSON_PARSE_RESULT_ERROR;
}

static int kason_stream_generic_type(kason_stream* stream, int* type) {
	char* begin = kason_stream_value_begin(stream);
	char* end = kason_stream_value_end(stream);

	if (end < begin) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (*begin == '-' || is_digit(*begin)) {
		if (!is_valid_number(begin, end)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		*type = KaSON_TYPE_NUMBER;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (kason_slice_equals(begin, end, "null")) {
		*type = KaSON_TYPE_NULL;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (kason_slice_equals(begin, end, "true")) {
		*type = KaSON_TYPE_TRUE;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (kason_slice_equals(begin, end, "false")) {
		*type = KaSON_TYPE_FALSE;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	return KaSON_PARSE_RESULT_ERROR;
}

static BOOL kason_stream_generic_delimiter(kason_stream* stream, char c) {
	if (stream->mode == M_OBJECT || stream->mode == M_ARRAY) {
		return c == KaSON_CHAR_PARAM_SEPARATOR || c == stream->end_token ||
			is_whitespace(c);
	}
	return stream->mode == M_GENERIC && is_whitespace(c);
}

static int kason_stream_commit_primitive(kason_stream* stream, int type) {
	char* begin = kason_stream_value_begin(stream);
	char* end = kason_stream_value_end(stream);
	int result = kason_stream_emit(stream, type, KaSON_STREAM_EVENT_VALUE, begin, end, TRUE);

	if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
		return result;
	}
	kason_stream_clear_scratch(stream);
	return KaSON_PARSE_RESULT_SUCCESS;
}

static void kason_stream_complete(kason_stream* stream) {
	stream->complete = TRUE;
	stream->state = KaSON_STREAM_STATE_COMPLETE;
}

static void kason_stream_after_completed_value(kason_stream* stream) {
	if (stream->mode == M_OBJECT || stream->mode == M_ARRAY) {
		stream->has_entries = TRUE;
		stream->state = KaSON_STREAM_STATE_FIND_PARAM_SEP;
	} else {
		kason_stream_complete(stream);
	}
}

static int kason_stream_commit_string(kason_stream* stream) {
	int result = kason_stream_commit_primitive(stream, KaSON_TYPE_STRING);

	if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
		return result;
	}
	kason_stream_after_completed_value(stream);
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_stream_commit_generic(kason_stream* stream, char delimiter) {
	int type;
	int result = kason_stream_generic_type(stream, &type);

	if (result != KaSON_PARSE_RESULT_SUCCESS) {
		return result;
	}
	result = kason_stream_commit_primitive(stream, type);
	if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
		return result;
	}
	if (stream->mode == M_GENERIC) {
		kason_stream_complete(stream);
	} else {
		stream->has_entries = TRUE;
		if (delimiter == KaSON_CHAR_PARAM_SEPARATOR) {
			stream->state = stream->mode == M_OBJECT
				? KaSON_STREAM_STATE_FIND_KEY_BEGIN
				: KaSON_STREAM_STATE_FIND_VALUE_BEGIN;
		} else if (delimiter == stream->end_token) {
			kason_stream_complete(stream);
		} else {
			stream->state = KaSON_STREAM_STATE_FIND_PARAM_SEP;
		}
	}
	return KaSON_PARSE_RESULT_SUCCESS;
}

static BOOL kason_stream_container_match(char open, char close) {
	return (open == KaSON_CHAR_OBJECT_START && close == KaSON_CHAR_OBJECT_END) ||
		(open == KaSON_CHAR_ARRAY_START && close == KaSON_CHAR_ARRAY_END);
}

static int kason_stream_container_push(kason_stream* stream, char c) {
	return kason_stream_append_scratch(stream, c);
}

static int kason_stream_start_container(kason_stream* stream, char* ptr) {
	int result;
	char c = *ptr;

	stream->container_type = c == KaSON_CHAR_OBJECT_START ? KaSON_TYPE_OBJECT : KaSON_TYPE_ARRAY;
	result = kason_stream_emit(stream, stream->container_type, KaSON_STREAM_EVENT_CONTAINER_BEGIN, ptr, ptr, TRUE);
	if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
		return result;
	}
	kason_stream_clear_scratch(stream);
	result = kason_stream_container_push(stream, c);
	if (result != KaSON_PARSE_RESULT_SUCCESS) {
		return result;
	}
	stream->container_string = FALSE;
	stream->container_escape = FALSE;
	stream->container_hex_left = 0;
	stream->container_unicode_value = 0;
	stream->container_high_surrogate = 0;
	stream->container_utf8_value = 0;
	stream->container_utf8_minimum = 0;
	stream->container_surrogate_state = KaSON_SURROGATE_NONE;
	stream->container_utf8_left = 0;
	stream->state = KaSON_STREAM_STATE_STREAM_CONTAINER;
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_stream_process_utf8(kason_stream* stream, unsigned char c) {
	if (stream->container_utf8_left == 0) {
		if (c < 0x80) {
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		if (c >= 0xc2 && c <= 0xdf) {
			stream->container_utf8_left = 1;
			stream->container_utf8_value = c & 0x1f;
			stream->container_utf8_minimum = 0x80;
		} else if (c >= 0xe0 && c <= 0xef) {
			stream->container_utf8_left = 2;
			stream->container_utf8_value = c & 0x0f;
			stream->container_utf8_minimum = 0x800;
		} else if (c >= 0xf0 && c <= 0xf4) {
			stream->container_utf8_left = 3;
			stream->container_utf8_value = c & 0x07;
			stream->container_utf8_minimum = 0x10000;
		} else {
			return KaSON_PARSE_RESULT_ERROR;
		}
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if ((c & 0xc0) != 0x80) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	stream->container_utf8_value = (stream->container_utf8_value << 6) | (c & 0x3f);
	stream->container_utf8_left--;
	if (stream->container_utf8_left == 0 &&
			(stream->container_utf8_value < stream->container_utf8_minimum ||
			 stream->container_utf8_value > 0x10ffff ||
			 (stream->container_utf8_value >= 0xd800 && stream->container_utf8_value <= 0xdfff))) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_stream_process_container_escape(kason_stream* stream, char c) {
	if (stream->container_hex_left > 0) {
		int digit = kason_hex_value(c);
		if (digit < 0) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		stream->container_unicode_value =
			(stream->container_unicode_value << 4) | (uint32_t)digit;
		stream->container_hex_left--;
		if (stream->container_hex_left == 0) {
			uint32_t value = stream->container_unicode_value;
			if (stream->container_surrogate_state == KaSON_SURROGATE_LOW_HEX) {
				if (value < 0xdc00 || value > 0xdfff) {
					return KaSON_PARSE_RESULT_ERROR;
				}
				stream->container_high_surrogate = 0;
				stream->container_surrogate_state = KaSON_SURROGATE_NONE;
				stream->container_escape = FALSE;
			} else if (value >= 0xd800 && value <= 0xdbff) {
				stream->container_high_surrogate = value;
				stream->container_surrogate_state = KaSON_SURROGATE_EXPECT_BACKSLASH;
				stream->container_escape = FALSE;
			} else if (value >= 0xdc00 && value <= 0xdfff) {
				return KaSON_PARSE_RESULT_ERROR;
			} else {
				stream->container_escape = FALSE;
			}
		}
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (c == 'u') {
		stream->container_hex_left = 4;
		stream->container_unicode_value = 0;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (is_simple_escape(c)) {
		stream->container_escape = FALSE;
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	return KaSON_PARSE_RESULT_ERROR;
}

static int kason_stream_process_container(kason_stream* stream, char* chunk, int length, int* index) {
	int i = *index;
	int part_start = i;

	while (i < length) {
		char c = chunk[i];
		BOOL final_close = FALSE;
		int result;

		if (stream->container_string) {
			if (stream->container_surrogate_state == KaSON_SURROGATE_EXPECT_BACKSLASH) {
				if (c != KaSON_CHAR_STRING_FMT_ESCAPE) {
					return KaSON_PARSE_RESULT_ERROR;
				}
				stream->container_surrogate_state = KaSON_SURROGATE_EXPECT_U;
			} else if (stream->container_surrogate_state == KaSON_SURROGATE_EXPECT_U) {
				if (c != 'u') {
					return KaSON_PARSE_RESULT_ERROR;
				}
				stream->container_surrogate_state = KaSON_SURROGATE_LOW_HEX;
				stream->container_hex_left = 4;
				stream->container_unicode_value = 0;
			} else if (stream->container_surrogate_state == KaSON_SURROGATE_LOW_HEX) {
				result = kason_stream_process_container_escape(stream, c);
				if (result != KaSON_PARSE_RESULT_SUCCESS) {
					return result;
				}
			} else if (stream->container_escape) {
				result = kason_stream_process_container_escape(stream, c);
				if (result != KaSON_PARSE_RESULT_SUCCESS) {
					return result;
				}
			} else if (stream->container_utf8_left > 0) {
				result = kason_stream_process_utf8(stream, (unsigned char)c);
				if (result != KaSON_PARSE_RESULT_SUCCESS) {
					return result;
				}
			} else if (c == KaSON_CHAR_STRING_FMT_ESCAPE) {
				stream->container_escape = TRUE;
				stream->container_hex_left = 0;
			} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
				stream->container_string = FALSE;
			} else if (is_control_char(c)) {
				return KaSON_PARSE_RESULT_ERROR;
			} else if ((unsigned char)c >= 0x80) {
				result = kason_stream_process_utf8(stream, (unsigned char)c);
				if (result != KaSON_PARSE_RESULT_SUCCESS) {
					return result;
				}
			}
		} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
			stream->container_string = TRUE;
			stream->container_escape = FALSE;
			stream->container_hex_left = 0;
			stream->container_unicode_value = 0;
			stream->container_high_surrogate = 0;
			stream->container_utf8_value = 0;
			stream->container_utf8_minimum = 0;
			stream->container_surrogate_state = KaSON_SURROGATE_NONE;
			stream->container_utf8_left = 0;
		} else if (c == KaSON_CHAR_OBJECT_START || c == KaSON_CHAR_ARRAY_START) {
			result = kason_stream_container_push(stream, c);
			if (result != KaSON_PARSE_RESULT_SUCCESS) {
				return result;
			}
		} else if (c == KaSON_CHAR_OBJECT_END || c == KaSON_CHAR_ARRAY_END) {
			if (stream->scratch_length <= 1 ||
					!kason_stream_container_match(stream->scratch[stream->scratch_length - 1], c)) {
				return KaSON_PARSE_RESULT_ERROR;
			}
			stream->scratch_length--;
			final_close = stream->scratch_length == 1;
		}
		if (final_close) {
			if (i > part_start) {
				result = kason_stream_emit(stream,
						stream->container_type,
						KaSON_STREAM_EVENT_CONTAINER_PART,
						chunk + part_start,
						chunk + i - 1,
						FALSE);
				if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
					*index = i + 1;
					return result;
				}
			}
			result = kason_stream_emit(stream,
					stream->container_type,
					KaSON_STREAM_EVENT_CONTAINER_END,
					chunk + i,
					chunk + i,
					FALSE);
			*index = i + 1;
			if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
				return result;
			}
			kason_stream_clear_scratch(stream);
			stream->has_entries = TRUE;
			stream->state = KaSON_STREAM_STATE_FIND_PARAM_SEP;
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		i++;
	}
	if (part_start < length) {
		int result = kason_stream_emit(stream,
				stream->container_type,
				KaSON_STREAM_EVENT_CONTAINER_PART,
				chunk + part_start,
				chunk + length - 1,
				FALSE);
		if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
			*index = length;
			return result;
		}
	}
	*index = length;
	return KaSON_PARSE_RESULT_SUCCESS;
}

void kason_stream_reset(kason_stream* stream) {
	if (NULL == stream) {
		return;
	}
	kason_stream_clear_scratch(stream);
	stream->mode = M_AUTO;
	stream->state = KaSON_STREAM_STATE_FIND_ROOT;
	stream->complete = FALSE;
	stream->callback_break = FALSE;
	stream->end_token = 0;
	stream->has_entries = FALSE;
	stream->container_type = KaSON_TYPE_UNKNOWN;
	stream->container_string = FALSE;
	stream->container_escape = FALSE;
	stream->container_hex_left = 0;
	stream->container_unicode_value = 0;
	stream->container_high_surrogate = 0;
	stream->container_utf8_value = 0;
	stream->container_utf8_minimum = 0;
	stream->container_surrogate_state = KaSON_SURROGATE_NONE;
	stream->container_utf8_left = 0;
}

int kason_stream_init(kason_stream* stream, char* scratch, int scratch_size, kason_stream_callback callback, void* user_data) {
	if (NULL == callback) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (NULL == stream || NULL == scratch || scratch_size <= 1) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	stream->scratch = scratch;
	stream->scratch_size = scratch_size;
	stream->callback = callback;
	stream->user_data = user_data;
	kason_stream_reset(stream);
	return KaSON_PARSE_RESULT_SUCCESS;
}

int kason_stream_feed(kason_stream* stream, char* chunk, int length) {
	int i;

	if (NULL == stream || NULL == stream->callback || NULL == stream->scratch || length < 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (length > 0 && NULL == chunk) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (stream->callback_break) {
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	for (i = 0; i < length;) {
		char c = chunk[i];
		int result = KaSON_PARSE_RESULT_SUCCESS;

		if (stream->state == KaSON_STREAM_STATE_COMPLETE) {
			if (!is_whitespace(c)) {
				return KaSON_PARSE_RESULT_ERROR;
			}
			i++;
			continue;
		}
		switch (stream->state) {
		case KaSON_STREAM_STATE_FIND_ROOT:
			if (is_whitespace(c)) {
				break;
			}
			if (c == KaSON_CHAR_OBJECT_START) {
				stream->mode = M_OBJECT;
				stream->end_token = KaSON_CHAR_OBJECT_END;
				stream->state = KaSON_STREAM_STATE_FIND_KEY_BEGIN;
			} else if (c == KaSON_CHAR_ARRAY_START) {
				stream->mode = M_ARRAY;
				stream->end_token = KaSON_CHAR_ARRAY_END;
				stream->state = KaSON_STREAM_STATE_FIND_VALUE_BEGIN;
			} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
				stream->mode = M_STRING;
				stream->direct_value_begin = chunk + i + 1;
				stream->direct_value_end = chunk + i;
				stream->state = KaSON_STREAM_STATE_FIND_STRING_END;
			} else {
				stream->mode = M_GENERIC;
				stream->direct_value_begin = chunk + i;
				stream->direct_value_end = chunk + i;
				stream->state = KaSON_STREAM_STATE_FIND_GENERIC_END;
			}
			break;
		case KaSON_STREAM_STATE_FIND_KEY_BEGIN:
			if (is_whitespace(c)) {
				break;
			}
			if (c == KaSON_CHAR_OBJECT_END && !stream->has_entries) {
				kason_stream_complete(stream);
			} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
				kason_stream_clear_scratch(stream);
				stream->direct_key_begin = chunk + i + 1;
				stream->direct_key_end = chunk + i;
				stream->state = KaSON_STREAM_STATE_FIND_KEY_END;
			} else {
				return KaSON_PARSE_RESULT_ERROR;
			}
			break;
		case KaSON_STREAM_STATE_FIND_KEY_END:
			if (c == KaSON_CHAR_STRING_FMT_ESCAPE) {
				result = kason_stream_preserve_direct(stream);
				if (result == KaSON_PARSE_RESULT_SUCCESS) {
					result = kason_stream_append_scratch(stream, c);
				}
				stream->state = KaSON_STREAM_STATE_KEY_ESCAPE;
				stream->escape_hex_left = 0;
			} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
				if (!kason_string_is_valid(
						kason_stream_key_begin(stream),
						kason_stream_key_end(stream))) {
					return KaSON_PARSE_RESULT_ERROR;
				}
				stream->has_key = TRUE;
				stream->state = KaSON_STREAM_STATE_FIND_VALUE_SEP;
			} else if (is_control_char(c)) {
				return KaSON_PARSE_RESULT_ERROR;
			} else if (stream->direct_key_begin != NULL) {
				while (i + 1 < length &&
						(unsigned char)chunk[i + 1] >= 0x20 &&
						chunk[i + 1] != KaSON_CHAR_STRING_SEPARATOR &&
						chunk[i + 1] != KaSON_CHAR_STRING_FMT_ESCAPE) {
					i++;
				}
				stream->direct_key_end = chunk + i;
			} else {
				result = kason_stream_append_scratch(stream, c);
				stream->key_end = stream->scratch_length - 1;
			}
			break;
		case KaSON_STREAM_STATE_KEY_ESCAPE:
			result = kason_stream_process_escape(stream, c, KaSON_STREAM_STATE_FIND_KEY_END);
			if (result == KaSON_PARSE_RESULT_SUCCESS) {
				stream->key_end = stream->scratch_length - 1;
			}
			break;
		case KaSON_STREAM_STATE_FIND_VALUE_SEP:
			if (is_whitespace(c)) {
				break;
			}
			if (c == KaSON_CHAR_KEY_VAL_SEPARATOR) {
				stream->state = KaSON_STREAM_STATE_FIND_VALUE_BEGIN;
			} else {
				return KaSON_PARSE_RESULT_ERROR;
			}
			break;
		case KaSON_STREAM_STATE_FIND_VALUE_BEGIN:
			if (is_whitespace(c)) {
				break;
			}
			if (c == stream->end_token) {
				if (stream->mode == M_ARRAY && !stream->has_entries) {
					kason_stream_complete(stream);
				} else {
					return KaSON_PARSE_RESULT_ERROR;
				}
			} else if (c == KaSON_CHAR_PARAM_SEPARATOR) {
				return KaSON_PARSE_RESULT_ERROR;
			} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
				stream->direct_value_begin = chunk + i + 1;
				stream->direct_value_end = chunk + i;
				stream->state = KaSON_STREAM_STATE_FIND_STRING_END;
			} else if (c == KaSON_CHAR_OBJECT_START || c == KaSON_CHAR_ARRAY_START) {
				result = kason_stream_start_container(stream, chunk + i);
			} else {
				stream->direct_value_begin = chunk + i;
				stream->direct_value_end = chunk + i;
				stream->state = KaSON_STREAM_STATE_FIND_GENERIC_END;
			}
			break;
		case KaSON_STREAM_STATE_FIND_STRING_END:
			if (c == KaSON_CHAR_STRING_FMT_ESCAPE) {
				result = kason_stream_preserve_direct(stream);
				if (result == KaSON_PARSE_RESULT_SUCCESS) {
					result = kason_stream_append_scratch(stream, c);
				}
				stream->state = KaSON_STREAM_STATE_STRING_ESCAPE;
				stream->escape_hex_left = 0;
			} else if (c == KaSON_CHAR_STRING_SEPARATOR) {
				if (!kason_string_is_valid(
						kason_stream_value_begin(stream),
						kason_stream_value_end(stream))) {
					return KaSON_PARSE_RESULT_ERROR;
				}
				result = kason_stream_commit_string(stream);
			} else if (is_control_char(c)) {
				return KaSON_PARSE_RESULT_ERROR;
			} else if (stream->direct_value_begin != NULL) {
				while (i + 1 < length &&
						(unsigned char)chunk[i + 1] >= 0x20 &&
						chunk[i + 1] != KaSON_CHAR_STRING_SEPARATOR &&
						chunk[i + 1] != KaSON_CHAR_STRING_FMT_ESCAPE) {
					i++;
				}
				stream->direct_value_end = chunk + i;
			} else {
				result = kason_stream_append_scratch(stream, c);
				stream->value_end = stream->scratch_length - 1;
			}
			break;
		case KaSON_STREAM_STATE_STRING_ESCAPE:
			result = kason_stream_process_escape(stream, c, KaSON_STREAM_STATE_FIND_STRING_END);
			if (result == KaSON_PARSE_RESULT_SUCCESS) {
				stream->value_end = stream->scratch_length - 1;
			}
			break;
		case KaSON_STREAM_STATE_FIND_GENERIC_END:
			if ((stream->mode == M_OBJECT || stream->mode == M_ARRAY) &&
					(c == KaSON_CHAR_PARAM_SEPARATOR || c == stream->end_token || is_whitespace(c))) {
				result = kason_stream_commit_generic(stream, c);
			} else if (stream->mode == M_GENERIC && is_whitespace(c)) {
				result = kason_stream_commit_generic(stream, c);
			} else if (stream->direct_value_begin != NULL) {
				while (i + 1 < length &&
						!kason_stream_generic_delimiter(stream, chunk[i + 1])) {
					i++;
				}
				stream->direct_value_end = chunk + i;
			} else {
				result = kason_stream_append_scratch(stream, c);
				stream->value_end = stream->scratch_length - 1;
			}
			break;
		case KaSON_STREAM_STATE_FIND_PARAM_SEP:
			if (is_whitespace(c)) {
				break;
			}
			if (c == KaSON_CHAR_PARAM_SEPARATOR) {
				stream->state = stream->mode == M_OBJECT
					? KaSON_STREAM_STATE_FIND_KEY_BEGIN
					: KaSON_STREAM_STATE_FIND_VALUE_BEGIN;
			} else if (c == stream->end_token) {
				kason_stream_complete(stream);
			} else {
				return KaSON_PARSE_RESULT_ERROR;
			}
			break;
		case KaSON_STREAM_STATE_STREAM_CONTAINER:
			result = kason_stream_process_container(stream, chunk, length, &i);
			if (result != KaSON_PARSE_RESULT_SUCCESS) {
				return result;
			}
			if (stream->callback_break) {
				return KaSON_PARSE_RESULT_SUCCESS;
			}
			continue;
		default:
			return KaSON_PARSE_RESULT_ERROR;
		}
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		if (stream->callback_break) {
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		i++;
	}
	{
		int preserve_result = kason_stream_preserve_direct(stream);
		if (preserve_result != KaSON_PARSE_RESULT_SUCCESS) {
			return preserve_result;
		}
	}
	return stream->state == KaSON_STREAM_STATE_COMPLETE
		? KaSON_PARSE_RESULT_SUCCESS
		: KaSON_PARSE_RESULT_INCOMPLETE;
}

int kason_stream_finish(kason_stream* stream) {
	int result;

	if (NULL == stream || NULL == stream->callback || NULL == stream->scratch) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (stream->callback_break) {
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (stream->state == KaSON_STREAM_STATE_COMPLETE) {
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (stream->state == KaSON_STREAM_STATE_FIND_GENERIC_END && stream->mode == M_GENERIC) {
		result = kason_stream_commit_generic(stream, 0);
		if (result != KaSON_PARSE_RESULT_SUCCESS || stream->callback_break) {
			return result;
		}
		return stream->state == KaSON_STREAM_STATE_COMPLETE
			? KaSON_PARSE_RESULT_SUCCESS
			: KaSON_PARSE_RESULT_ERROR;
	}
	return KaSON_PARSE_RESULT_ERROR;
}

static int KaSON_HOT_ALIGN kason_parse_internal(char* begin, char* end, kason_parse_callback callback,
		void* user_data, KaSON_PARSE_MODE mode, int depth, BOOL validate_containers,
		kason_lookup_context* lookup) {
	if (NULL == callback && NULL == lookup) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (NULL == begin) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (depth > KaSON_MAX_NESTING) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	int state;
	char end_token = 0;
	BOOL root_array = FALSE;

	if (M_AUTO == mode) {
		for (; (end ? begin <= end : *begin != 0) && is_whitespace(*begin); begin++) {
		}
		if (end ? begin > end : *begin == 0) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		switch (*begin) {
		case KaSON_CHAR_OBJECT_START:
			mode = M_OBJECT;
			break;
		case KaSON_CHAR_ARRAY_START:
			root_array = TRUE;
			mode = M_ARRAY;
			break;
		case KaSON_CHAR_STRING_SEPARATOR:
			mode = M_STRING;
			break;
		default:
			mode = M_GENERIC;
			break;
		}
	}

	switch (mode) {
	case M_OBJECT:
		state = KaSON_STATE_FIND_OBJECT_BEGIN;
		end_token = KaSON_CHAR_OBJECT_END;
		break;
	case M_ARRAY:
		state = KaSON_STATE_FIND_VALUE_BEGIN;
		end_token = KaSON_CHAR_ARRAY_END;
		break;
	case M_STRING:
		state = KaSON_STATE_FIND_VALUE_BEGIN;
		break;
	case M_GENERIC:
		state = KaSON_STATE_FIND_VALUE_BEGIN;
		break;
	case M_AUTO:
		// impossible
		return KaSON_PARSE_RESULT_SUCCESS;
	}

	char* key_begin = NULL;
	char* key_end = NULL;
	char* value_begin = NULL;
	char* value_end = NULL;
	BOOL key_needs_validation = FALSE;
	int key_decoded_length = 0;
	uint32_t key_hash = 0;
	const kason_lookup_key* matched_key = NULL;
	int selected_conversion = KaSON_CACHED_NUMBER_NONE;
	kason_number_accumulator number_accumulator;
	kason_cached_number cached_number = {KaSON_CACHED_NUMBER_NONE,
		KaSON_CONVERT_ERROR, {0}};
	BOOL value_needs_validation = FALSE;
	kason_number_state number_state = KaSON_NUMBER_INVALID;
	BOOL commit = FALSE;
	int chars_to_omit = 0;
	int array_bracket_counter = 0;
	int object_bracket_counter = 0;
	int array_elements = 0;
	BOOL object_has_entries = FALSE;
	char* last_non_whitespace_ptr = begin;
	char* ptr;
	for(ptr = begin; (end ? ptr <= end : *ptr != 0) && state!=KaSON_STATE_COMPLETE; ptr++) {
		BOOL last_token = end ? ptr == end : *(ptr + 1) == 0;
		LOG("[%3d]parse_json:%3d: *ptr='%c' l!ws[%3d]='%c' state:%2d mode:%d cto:%2d abc:%d obc:%d ae:%d lt:%d\n",
				(int) (ptr - begin), __LINE__, *ptr, (int) (last_non_whitespace_ptr - begin), *last_non_whitespace_ptr, state, mode,
				chars_to_omit, array_bracket_counter, object_bracket_counter, array_elements, last_token);
		switch(state) {
		case KaSON_STATE_FIND_OBJECT_BEGIN:
			if (KaSON_CHAR_OBJECT_START == *ptr) {
				state = KaSON_STATE_FIND_KEY_BEGIN;
			}
			break;
		case KaSON_STATE_FIND_KEY_BEGIN:
			if (KaSON_CHAR_OBJECT_END == *ptr && !object_has_entries) {
				state = KaSON_STATE_COMPLETE;
			} else if (check_begin(*ptr, &state)) {
				key_begin = ptr + 1; // pointer to begin of key string
				key_needs_validation = FALSE;
				key_decoded_length = 0;
				key_hash = 0;
			}
			break;
		case KaSON_STATE_FIND_KEY_END:
			if (*ptr == KaSON_CHAR_STRING_FMT_ESCAPE) {
				key_needs_validation = TRUE;
				state = KaSON_STATE_KEY_FORMAT_CHAR;
			} else if (KaSON_CHAR_STRING_SEPARATOR == *ptr) {
				key_end = ptr - 1; // pointer to end of key string
				if (key_needs_validation) {
					if (lookup != NULL) {
						kason_key key = {key_begin, key_end};
						if (!kason_decoded_key_info(&key, &key_hash,
								&key_decoded_length)) {
							state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
							break;
						}
					} else if (!kason_string_is_valid(key_begin, key_end)) {
						state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
						break;
					}
				}
				if (lookup != NULL && !key_needs_validation) {
					key_decoded_length = key_end >= key_begin
						? (int)(key_end - key_begin + 1) : 0;
					key_hash = kason_hash_bytes(key_begin, key_decoded_length);
				}
				if (lookup != NULL) {
					kason_key key = {key_begin, key_end};
					matched_key = kason_lookup_find(lookup->table, &key,
						key_hash, key_decoded_length);
					selected_conversion = matched_key != NULL &&
						lookup->selector != NULL
						? lookup->selector(matched_key, lookup->user_data)
						: KaSON_CACHED_NUMBER_NONE;
				}
				state = KaSON_STATE_FIND_VALUE_SEPARATOR;
			} else if (is_control_char(*ptr)) {
				state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
			} else if ((unsigned char)*ptr >= 0x80) {
				key_needs_validation = TRUE;
			} else {
				ptr = kason_skip_plain_string(ptr, end);
			}
			break;
		case KaSON_STATE_FIND_VALUE_SEPARATOR:
			if (KaSON_CHAR_KEY_VAL_SEPARATOR == *ptr) {
				state = KaSON_STATE_FIND_VALUE_BEGIN;
			} else if (!is_whitespace(*ptr)) {
				state = KaSON_STATE_ERR_INCORRECT_CHAR;
			}
			break;
		case KaSON_STATE_FIND_VALUE_BEGIN:
			if (check_begin(*ptr, &state)) {
				if (state == KaSON_STATE_FIND_STRING_VALUE_END || state == KaSON_STATE_FIND_ARRAY_VALUE_END) {
					value_begin = ptr + 1;
				} else if (state > 0) { // non error state
					value_begin = ptr;
				}
				if (state == KaSON_STATE_FIND_STRING_VALUE_END) {
					value_needs_validation = FALSE;
				} else if (state == KaSON_STATE_FIND_GENERIC_VALUE_END) {
					number_state = kason_number_begin(*ptr);
					if (number_state != KaSON_NUMBER_INVALID) {
						state = KaSON_STATE_FIND_NUMBER_VALUE_END;
					}
					if (state == KaSON_STATE_FIND_NUMBER_VALUE_END &&
							selected_conversion !=
							KaSON_CACHED_NUMBER_NONE) {
						kason_number_accumulator_init(&number_accumulator,
							selected_conversion);
						kason_number_accumulator_feed(&number_accumulator,
							*ptr, number_state);
					} else if (state == KaSON_STATE_FIND_GENERIC_VALUE_END) {
						ptr = kason_skip_keyword(ptr, end);
					}
				}
				if (state == KaSON_STATE_FIND_ARRAY_VALUE_END) {
					array_bracket_counter = 1;
					object_bracket_counter = 0;
					array_elements = 0;
				} else if (state == KaSON_STATE_FIND_OBJECT_VALUE_END) {
					array_bracket_counter = 0;
					object_bracket_counter = 1;
				} else if ((state == KaSON_STATE_FIND_GENERIC_VALUE_END ||
						state == KaSON_STATE_FIND_NUMBER_VALUE_END) && last_token) {
					value_end = ptr;
					commit = TRUE;
				}
			}
			break;
		case KaSON_STATE_FIND_GENERIC_VALUE_END:
			if (*ptr == KaSON_CHAR_PARAM_SEPARATOR || *ptr == end_token || is_whitespace(*ptr) || last_token) {
				value_end = (last_token && *ptr != end_token && !is_whitespace(*ptr))
					? ptr
					: ptr - 1;
				commit = TRUE;
			}
			break;
		case KaSON_STATE_FIND_NUMBER_VALUE_END:
			if (*ptr == KaSON_CHAR_PARAM_SEPARATOR || *ptr == end_token || is_whitespace(*ptr) || last_token) {
				value_end = (last_token && *ptr != end_token && !is_whitespace(*ptr))
					? ptr
					: ptr - 1;
				if (last_token && *ptr != end_token && !is_whitespace(*ptr)) {
					if (!is_digit(*ptr) ||
							(number_state != KaSON_NUMBER_INTEGER &&
							number_state != KaSON_NUMBER_FRACTION &&
							number_state != KaSON_NUMBER_EXPONENT)) {
						number_state = kason_number_next(number_state, *ptr);
					}
					if (selected_conversion != KaSON_CACHED_NUMBER_NONE) {
						kason_number_accumulator_feed(&number_accumulator,
							*ptr, number_state);
					}
				}
				commit = TRUE;
			} else {
				if (!is_digit(*ptr) ||
						(number_state != KaSON_NUMBER_INTEGER &&
						number_state != KaSON_NUMBER_FRACTION &&
						number_state != KaSON_NUMBER_EXPONENT)) {
					number_state = kason_number_next(number_state, *ptr);
				}
				if (selected_conversion != KaSON_CACHED_NUMBER_NONE) {
					kason_number_accumulator_feed(&number_accumulator,
						*ptr, number_state);
				} else if (is_digit(*ptr) &&
						(number_state == KaSON_NUMBER_INTEGER ||
						number_state == KaSON_NUMBER_FRACTION ||
						number_state == KaSON_NUMBER_EXPONENT)) {
					ptr = kason_skip_number_digits(ptr, end);
				}
			}
			break;
		case KaSON_STATE_FIND_STRING_VALUE_END:
			if (*ptr == KaSON_CHAR_STRING_FMT_ESCAPE) {
				value_needs_validation = TRUE;
				state = KaSON_STATE_STRING_VALUE_FORMAT_CHAR;
			} else if (*ptr == KaSON_CHAR_STRING_SEPARATOR) {
				value_end = ptr - 1;
				if (value_needs_validation && !kason_string_is_valid(value_begin, value_end)) {
					state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
				} else {
					commit = TRUE;
				}
			} else if (is_control_char(*ptr)) {
				state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
			} else if ((unsigned char)*ptr >= 0x80) {
				value_needs_validation = TRUE;
			} else {
				ptr = kason_skip_plain_string(ptr, end);
			}
			break;
		case KaSON_STATE_STRING_VALUE_FORMAT_CHAR:
		case KaSON_STATE_ASTRING_VALUE_FORMAT_CHAR:
		case KaSON_STATE_OSTRING_VALUE_FORMAT_CHAR:
		case KaSON_STATE_KEY_FORMAT_CHAR:
			if (chars_to_omit > 0) {
				if (!is_hex_digit(*ptr)) {
					state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
				} else {
					chars_to_omit--;
					if (chars_to_omit == 0) {
						restore_escaped_state(&state);
					}
				}
			} else if (*ptr == 'u') {
				chars_to_omit = 4;
			} else if (is_simple_escape(*ptr)) {
				restore_escaped_state(&state);
			} else {
				state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
			}
			break;
		case KaSON_STATE_FIND_ARRAY_VALUE_END:
			if (check_end(*ptr, &array_bracket_counter, &object_bracket_counter, &state)) {
				// find end of array without tailing whitespace chars
				if (last_non_whitespace_ptr >= value_begin && *last_non_whitespace_ptr == KaSON_CHAR_PARAM_SEPARATOR) {
					state = KaSON_STATE_ERR_INCORRECT_CHAR;
					break;
				}
				value_end = last_non_whitespace_ptr; //ptr - 1; // omit closing bracket
				commit = TRUE;
			} else if(*ptr == KaSON_CHAR_PARAM_SEPARATOR && object_bracket_counter == 0 && array_bracket_counter == 1) {
				// if not the end of array and current char is parameter separator and not inside object or another array
				array_elements ++;
			}
			break;
		case KaSON_STATE_FIND_ARRAY_VALUE_END_STR:
			if (*ptr == KaSON_CHAR_STRING_FMT_ESCAPE) {
				state = KaSON_STATE_ASTRING_VALUE_FORMAT_CHAR;
			} else if (*ptr == KaSON_CHAR_STRING_SEPARATOR) {
				state = KaSON_STATE_FIND_ARRAY_VALUE_END;
			} else if (is_control_char(*ptr)) {
				state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
			} else if ((unsigned char)*ptr < 0x80) {
				ptr = kason_skip_plain_string(ptr, end);
			}
			break;
		case KaSON_STATE_FIND_OBJECT_VALUE_END:
			if (check_end(*ptr, &array_bracket_counter, &object_bracket_counter, &state)) {
				value_end = ptr;
				commit = TRUE;
			}
			break;
		case KaSON_STATE_FIND_OBJECT_VALUE_END_STR:
			if (*ptr == KaSON_CHAR_STRING_FMT_ESCAPE) {
				state = KaSON_STATE_OSTRING_VALUE_FORMAT_CHAR;
			} else if (*ptr == KaSON_CHAR_STRING_SEPARATOR) {
				state = KaSON_STATE_FIND_OBJECT_VALUE_END;
			} else if (is_control_char(*ptr)) {
				state = KaSON_STATE_ERR_INCORRECT_FORMAT_CHAR;
			} else if ((unsigned char)*ptr < 0x80) {
				ptr = kason_skip_plain_string(ptr, end);
			}
			break;
		case KaSON_STATE_FIND_PARAM_SEPARATOR:
			if (KaSON_CHAR_PARAM_SEPARATOR == *ptr) {
				switch (mode) {
				case M_OBJECT:
					state = KaSON_STATE_FIND_KEY_BEGIN;
					break;
				case M_ARRAY:
				case M_STRING:
				case M_GENERIC:
					state = KaSON_STATE_FIND_VALUE_BEGIN;
					break;
				default:
					// impossible
					break;
				}
			} else if (mode == M_OBJECT && KaSON_CHAR_OBJECT_END == *ptr) {
				state = KaSON_STATE_COMPLETE;
			} else if (!is_whitespace(*ptr)) {
				state = KaSON_STATE_ERR_INCORRECT_CHAR;
			}
			break;
		}
		if (state < 0) { // error occured
			return (-state & 0xff) | KaSON_PARSE_RESULT_ERROR;
		} else if (commit) {
			int type = KaSON_TYPE_UNKNOWN;
			int count = 1;
			switch(state) {
			case KaSON_STATE_FIND_STRING_VALUE_END:
				type = KaSON_TYPE_STRING;
				break;
			case KaSON_STATE_FIND_ARRAY_VALUE_END:
				type = KaSON_TYPE_ARRAY;
				count = (value_begin != NULL && value_end != NULL && value_begin <= value_end)
					? array_elements + 1
					: 0;
				if (validate_containers && count > 0 &&
						kason_parse_internal(value_begin, value_end, kason_validate_callback,
							NULL, M_ARRAY, depth + (root_array ? 0 : 1), TRUE, NULL) !=
							KaSON_PARSE_RESULT_SUCCESS) {
					return KaSON_PARSE_RESULT_ERROR;
				}
				break;
			case KaSON_STATE_FIND_OBJECT_VALUE_END:
				type = KaSON_TYPE_OBJECT;
				if (validate_containers &&
						kason_parse_internal(value_begin, value_end, kason_validate_callback,
							NULL, M_AUTO, depth + 1, TRUE, NULL) != KaSON_PARSE_RESULT_SUCCESS) {
					return KaSON_PARSE_RESULT_ERROR;
				}
				break;
		case KaSON_STATE_FIND_NUMBER_VALUE_END:
			if (NULL == value_begin || NULL == value_end || value_begin > value_end) {
				return KaSON_PARSE_RESULT_ERROR;
			} else if (!kason_number_complete(number_state)) {
				return KaSON_PARSE_RESULT_ERROR;
			}
			type = KaSON_TYPE_NUMBER;
			break;
		case KaSON_STATE_FIND_GENERIC_VALUE_END:
			if (NULL == value_begin || NULL == value_end || value_begin > value_end) {
				return KaSON_PARSE_RESULT_ERROR;
			} else if (kason_slice_equals(value_begin, value_end, "null")) {
				type = KaSON_TYPE_NULL;
			} else if (kason_slice_equals(value_begin, value_end, "true")) {
				type = KaSON_TYPE_TRUE;
			} else if (kason_slice_equals(value_begin, value_end, "false")) {
				type = KaSON_TYPE_FALSE;
			} else {
				return KaSON_PARSE_RESULT_ERROR;
			}
			break;
			}
			kason_data data = {type, value_begin, value_end,
				KaSON_STREAM_EVENT_VALUE};
			int callback_result;
			if (type == KaSON_TYPE_NUMBER && selected_conversion !=
					KaSON_CACHED_NUMBER_NONE) {
				kason_number_accumulator_finish(&number_accumulator, &cached_number);
			} else {
				cached_number.kind = KaSON_CACHED_NUMBER_NONE;
				cached_number.result = KaSON_CONVERT_ERROR;
			}
			if (lookup != NULL) {
				if (matched_key == NULL) {
					callback_result = KaSON_CALLBACK_CONTINUE;
				} else if (lookup->converted_callback != NULL) {
					callback_result = lookup->converted_callback(matched_key,
						&data, count, &cached_number, lookup->user_data);
				} else {
					callback_result = lookup->callback(matched_key, &data,
						count, lookup->user_data);
				}
			} else if (key_begin == NULL) {
				callback_result = callback(NULL, &data, count, user_data);
			} else {
				kason_key key = {key_begin, key_end};
				callback_result = callback(&key, &data, count, user_data);
			}
			if (callback_result == KaSON_CALLBACK_BREAK) {
				return KaSON_PARSE_RESULT_SUCCESS;
			}
			key_begin = key_end = value_begin = value_end = NULL;
			matched_key = NULL;
			selected_conversion = KaSON_CACHED_NUMBER_NONE;
			commit = FALSE;
			switch(mode) {
			case M_OBJECT:
				object_has_entries = TRUE;
				if (state != KaSON_STATE_FIND_OBJECT_VALUE_END && KaSON_CHAR_OBJECT_END == *ptr) {
					state = KaSON_STATE_COMPLETE;
				} else if (KaSON_CHAR_PARAM_SEPARATOR == *ptr) {
					state = KaSON_STATE_FIND_KEY_BEGIN;
				} else {
					state = KaSON_STATE_FIND_PARAM_SEPARATOR;
				}
				break;
			case M_ARRAY:
				if (root_array) {
					state = KaSON_STATE_COMPLETE;
				} else if (KaSON_CHAR_PARAM_SEPARATOR == *ptr) {
					state = KaSON_STATE_FIND_VALUE_BEGIN;
				} else {
					state = KaSON_STATE_FIND_PARAM_SEPARATOR;
				}
				break;
			case M_STRING:
			case M_GENERIC:
				state = KaSON_STATE_COMPLETE;
				break;
			case M_AUTO:
				// impossible
				break;
			}
		}
		if (!is_whitespace(*ptr)) {
			last_non_whitespace_ptr = ptr;
		}
	}
	if (state == KaSON_STATE_COMPLETE) {
		for (; end ? ptr <= end : *ptr != 0; ptr++) {
			if (!is_whitespace(*ptr)) {
				return KaSON_PARSE_RESULT_ERROR;
			}
		}
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (mode == M_ARRAY && state == KaSON_STATE_FIND_PARAM_SEPARATOR) {
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	return KaSON_PARSE_RESULT_ERROR;
}

typedef struct s_kason_scan_context {
	char* end;
	kason_parse_callback callback;
	void* user_data;
	int stopped;
} kason_scan_context;

static BOOL kason_scan_has(kason_scan_context* scan, char* ptr) {
	return scan->end != NULL ? ptr <= scan->end : *ptr != 0;
}

static void kason_scan_whitespace(kason_scan_context* scan, char** cursor) {
	while (kason_scan_has(scan, *cursor) && is_whitespace(**cursor)) {
		(*cursor)++;
	}
}

static int kason_scan_string(kason_scan_context* scan, char** cursor,
		char** value_begin, char** value_end) {
	char* opening = *cursor;
	char* ptr = opening + 1;
	BOOL escape = FALSE;
	BOOL needs_validation = FALSE;

	while (kason_scan_has(scan, ptr)) {
		if (!escape && *ptr == KaSON_CHAR_STRING_SEPARATOR) {
			*value_begin = opening + 1;
			*value_end = ptr - 1;
			if (needs_validation && *value_begin <= *value_end &&
					!kason_string_is_valid(*value_begin, *value_end)) {
				return KaSON_PARSE_RESULT_ERROR;
			}
			*cursor = ptr + 1;
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		if (!escape && is_control_char(*ptr)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		if (!escape && *ptr == KaSON_CHAR_STRING_FMT_ESCAPE) {
			needs_validation = TRUE;
			escape = TRUE;
		} else {
			if ((unsigned char)*ptr >= 0x80) {
				needs_validation = TRUE;
			} else if (!escape) {
				ptr = kason_skip_plain_string(ptr, scan->end);
			}
			escape = FALSE;
		}
		ptr++;
	}
	return KaSON_PARSE_RESULT_ERROR;
}

static int kason_scan_emit(kason_scan_context* scan, kason_key* key,
		int type, int event, char* begin, char* end, int count) {
	kason_data data = {type, begin, end, event};
	int action;

	if (scan->callback == NULL) {
		return event == KaSON_STREAM_EVENT_CONTAINER_BEGIN
			? KaSON_ACTION_SKIP : KaSON_CALLBACK_CONTINUE;
	}
	action = scan->callback(key, &data, count, scan->user_data);
	if (action == KaSON_ACTION_BREAK) {
		scan->stopped = TRUE;
	}
	return action;
}

static int kason_scan_value(kason_scan_context* scan, char** cursor,
		kason_key* key, BOOL emit, int depth);
static int kason_scan_primitive(kason_scan_context* scan, char** cursor,
		kason_key* key, BOOL emit);

static int kason_scan_number(kason_scan_context* scan, char** cursor,
		char** value_end) {
	char* ptr = *cursor;

	if (*ptr == '-') {
		ptr++;
		if (!kason_scan_has(scan, ptr)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
	}
	if (*ptr == '0') {
		ptr++;
	} else if (*ptr >= '1' && *ptr <= '9') {
		for (ptr++; kason_scan_has(scan, ptr) && is_digit(*ptr); ptr++) {
		}
	} else {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (kason_scan_has(scan, ptr) && *ptr == '.') {
		ptr++;
		if (!kason_scan_has(scan, ptr) || !is_digit(*ptr)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		for (ptr++; kason_scan_has(scan, ptr) && is_digit(*ptr); ptr++) {
		}
	}
	if (kason_scan_has(scan, ptr) && (*ptr == 'e' || *ptr == 'E')) {
		ptr++;
		if (kason_scan_has(scan, ptr) && (*ptr == '+' || *ptr == '-')) {
			ptr++;
		}
		if (!kason_scan_has(scan, ptr) || !is_digit(*ptr)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		for (ptr++; kason_scan_has(scan, ptr) && is_digit(*ptr); ptr++) {
		}
	}
	*value_end = ptr - 1;
	*cursor = ptr;
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_scan_literal(kason_scan_context* scan, char** cursor,
		char** value_end, int* type) {
	char* begin = *cursor;
	const char* literal;
	int length;
	int i;

	if (*begin == 't') {
		literal = "true";
		length = 4;
		*type = KaSON_TYPE_TRUE;
	} else if (*begin == 'f') {
		literal = "false";
		length = 5;
		*type = KaSON_TYPE_FALSE;
	} else if (*begin == 'n') {
		literal = "null";
		length = 4;
		*type = KaSON_TYPE_NULL;
	} else {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (scan->end != NULL && scan->end - begin < length - 1) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	for (i = 1; i < length; i++) {
		if ((scan->end == NULL && begin[i] == 0) ||
				begin[i] != literal[i]) {
			return KaSON_PARSE_RESULT_ERROR;
		}
	}
	*value_end = begin + length - 1;
	*cursor = begin + length;
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_scan_container(kason_scan_context* scan, char** cursor,
		kason_key* key, BOOL emit, int depth, int type) {
	char* opening = *cursor;
	char close = type == KaSON_TYPE_OBJECT
		? KaSON_CHAR_OBJECT_END : KaSON_CHAR_ARRAY_END;
	int action = emit ? kason_scan_emit(scan, key, type,
		KaSON_STREAM_EVENT_CONTAINER_BEGIN, opening, NULL, 0)
		: KaSON_ACTION_SKIP;
	int count = 0;
	int result;

	if (scan->stopped) {
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	if (action != KaSON_ACTION_ENTER && action != KaSON_ACTION_CAPTURE &&
			action != KaSON_ACTION_SKIP) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	(*cursor)++;
	kason_scan_whitespace(scan, cursor);
	if (kason_scan_has(scan, *cursor) && **cursor == close) {
		char* closing = (*cursor)++;
		if (emit && action != KaSON_ACTION_SKIP) {
			char* begin = action == KaSON_ACTION_CAPTURE ? opening : closing;
			result = kason_scan_emit(scan, key, type,
				KaSON_STREAM_EVENT_CONTAINER_END, begin, closing,
				type == KaSON_TYPE_ARRAY ? 0 : 1);
			if (result != KaSON_CALLBACK_CONTINUE && result != KaSON_ACTION_BREAK) {
				return KaSON_PARSE_RESULT_ERROR;
			}
		}
		return KaSON_PARSE_RESULT_SUCCESS;
	}
	while (kason_scan_has(scan, *cursor)) {
		kason_key child_key;
		kason_key* child_key_ptr = NULL;
		if (type == KaSON_TYPE_OBJECT) {
			if (**cursor != KaSON_CHAR_STRING_SEPARATOR) {
				return KaSON_PARSE_RESULT_ERROR;
			}
			result = kason_scan_string(scan, cursor,
				&child_key.begin, &child_key.end);
			if (result != KaSON_PARSE_RESULT_SUCCESS) {
				return result;
			}
			child_key_ptr = &child_key;
			kason_scan_whitespace(scan, cursor);
			if (!kason_scan_has(scan, *cursor) ||
					**cursor != KaSON_CHAR_KEY_VAL_SEPARATOR) {
				return KaSON_PARSE_RESULT_ERROR;
			}
			(*cursor)++;
			kason_scan_whitespace(scan, cursor);
		}
		if (depth + 1 > KaSON_MAX_NESTING) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		if (**cursor == KaSON_CHAR_OBJECT_START ||
				**cursor == KaSON_CHAR_ARRAY_START) {
			result = kason_scan_value(scan, cursor, child_key_ptr,
				emit && action == KaSON_ACTION_ENTER, depth + 1);
		} else {
			result = kason_scan_primitive(scan, cursor, child_key_ptr,
				emit && action == KaSON_ACTION_ENTER);
		}
		if (result != KaSON_PARSE_RESULT_SUCCESS || scan->stopped) {
			return result;
		}
		count++;
		kason_scan_whitespace(scan, cursor);
		if (!kason_scan_has(scan, *cursor)) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		if (**cursor == close) {
			char* closing = (*cursor)++;
			if (emit && action != KaSON_ACTION_SKIP) {
				char* begin = action == KaSON_ACTION_CAPTURE ? opening : closing;
				result = kason_scan_emit(scan, key, type,
					KaSON_STREAM_EVENT_CONTAINER_END, begin, closing,
					type == KaSON_TYPE_ARRAY ? count : 1);
				if (result != KaSON_CALLBACK_CONTINUE &&
						result != KaSON_ACTION_BREAK) {
					return KaSON_PARSE_RESULT_ERROR;
				}
			}
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		if (**cursor != KaSON_CHAR_PARAM_SEPARATOR) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		(*cursor)++;
		kason_scan_whitespace(scan, cursor);
		if (!kason_scan_has(scan, *cursor) || **cursor == close) {
			return KaSON_PARSE_RESULT_ERROR;
		}
	}
	return KaSON_PARSE_RESULT_ERROR;
}

static int kason_scan_primitive(kason_scan_context* scan, char** cursor,
		kason_key* key, BOOL emit) {
	char* begin;
	char* end;
	int type;
	int result;

	if (!kason_scan_has(scan, *cursor)) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	begin = *cursor;
	if (*begin == KaSON_CHAR_STRING_SEPARATOR) {
		result = kason_scan_string(scan, cursor, &begin, &end);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		type = KaSON_TYPE_STRING;
	} else if (*begin == '-' || is_digit(*begin)) {
		result = kason_scan_number(scan, cursor, &end);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
		type = KaSON_TYPE_NUMBER;
	} else if (*begin == 't' || *begin == 'f' || *begin == 'n') {
		result = kason_scan_literal(scan, cursor, &end, &type);
		if (result != KaSON_PARSE_RESULT_SUCCESS) {
			return result;
		}
	} else {
		for (end = begin; kason_scan_has(scan, end) &&
				!is_whitespace(*end) && *end != KaSON_CHAR_PARAM_SEPARATOR &&
				*end != KaSON_CHAR_OBJECT_END && *end != KaSON_CHAR_ARRAY_END;
				end++) {
		}
		*cursor = end;
		end--;
		if (end < begin) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		if (kason_slice_equals(begin, end, "null")) {
			type = KaSON_TYPE_NULL;
		} else if (kason_slice_equals(begin, end, "true")) {
			type = KaSON_TYPE_TRUE;
		} else if (kason_slice_equals(begin, end, "false")) {
			type = KaSON_TYPE_FALSE;
		} else {
			return KaSON_PARSE_RESULT_ERROR;
		}
	}
	if (emit) {
		result = kason_scan_emit(scan, key, type,
			KaSON_STREAM_EVENT_VALUE, begin, end, 1);
		if (result != KaSON_CALLBACK_CONTINUE && result != KaSON_ACTION_BREAK) {
			return KaSON_PARSE_RESULT_ERROR;
		}
	}
	return KaSON_PARSE_RESULT_SUCCESS;
}

static int kason_scan_value(kason_scan_context* scan, char** cursor,
		kason_key* key, BOOL emit, int depth) {
	if (depth > KaSON_MAX_NESTING) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	kason_scan_whitespace(scan, cursor);
	if (!kason_scan_has(scan, *cursor)) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (**cursor == KaSON_CHAR_OBJECT_START) {
		return kason_scan_container(scan, cursor, key, emit, depth,
			KaSON_TYPE_OBJECT);
	}
	if (**cursor == KaSON_CHAR_ARRAY_START) {
		return kason_scan_container(scan, cursor, key, emit, depth,
			KaSON_TYPE_ARRAY);
	}
	return kason_scan_primitive(scan, cursor, key, emit);
}

static int kason_scan_parse(char* begin, char* end,
		kason_parse_callback callback, void* user_data) {
	kason_scan_context scan = {end, callback, user_data, FALSE};
	char* cursor = begin;
	int result;

	if (callback == NULL) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (begin == NULL) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	result = kason_scan_value(&scan, &cursor, NULL, TRUE, 0);
	if (result != KaSON_PARSE_RESULT_SUCCESS || scan.stopped) {
		return result;
	}
	kason_scan_whitespace(&scan, &cursor);
	return kason_scan_has(&scan, cursor)
		? KaSON_PARSE_RESULT_ERROR : KaSON_PARSE_RESULT_SUCCESS;
}

int kason_parse_range(char* begin, char* end, kason_parse_callback callback, void* user_data) {
	return kason_scan_parse(begin, end, callback, user_data);
}

int kason_parse(char* kason_buf, kason_parse_callback callback, void* user_data) {
	size_t length;

	if (kason_buf == NULL) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	length = strlen(kason_buf);
	if (length == 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	return kason_scan_parse(kason_buf, kason_buf + length - 1,
		callback, user_data);
}

int kason_lookup_key_init(kason_lookup_key* key, const char* value, int length) {
	if (key == NULL || value == NULL || length < 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	key->value = value;
	key->length = length;
	key->hash = kason_hash_bytes(value, length);
	return KaSON_PARSE_RESULT_SUCCESS;
}

int kason_lookup_table_init(kason_lookup_table* table, kason_lookup_key slots[],
		int capacity) {
	int i;

	if (table == NULL || slots == NULL || capacity <= 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	for (i = 0; i < capacity; i++) {
		slots[i].value = NULL;
		slots[i].length = 0;
		slots[i].hash = 0;
	}
	table->slots = slots;
	table->capacity = capacity;
	table->count = 0;
	return KaSON_PARSE_RESULT_SUCCESS;
}

int kason_lookup_table_add(kason_lookup_table* table, const char* value, int length) {
	kason_lookup_key key;
	int index;
	int probes;

	if (table == NULL || table->slots == NULL || table->capacity <= 0 ||
			table->count >= table->capacity ||
			kason_lookup_key_init(&key, value, length) != KaSON_PARSE_RESULT_SUCCESS) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	index = (int)(key.hash % (uint32_t)table->capacity);
	for (probes = 0; probes < table->capacity; probes++) {
		kason_lookup_key* slot = &table->slots[index];
		if (slot->value == NULL) {
			*slot = key;
			table->count++;
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		if (slot->hash == key.hash && slot->length == key.length &&
				memcmp(slot->value, key.value, (size_t)key.length) == 0) {
			return KaSON_PARSE_RESULT_ERROR;
		}
		index++;
		if (index == table->capacity) {
			index = 0;
		}
	}
	return KaSON_PARSE_RESULT_ERROR;
}

int kason_parse_range_selected(char* begin, char* end,
		const kason_lookup_table* table,
		kason_lookup_callback callback, void* user_data) {
	kason_lookup_context context;

	if (callback == NULL) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (table == NULL || table->slots == NULL || table->capacity <= 0 ||
			table->count <= 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	context.table = table;
	context.callback = callback;
	context.selector = NULL;
	context.converted_callback = NULL;
	context.user_data = user_data;
	return kason_parse_internal(begin, end, NULL, NULL,
		M_AUTO, 0, TRUE, &context);
}

int kason_parse_selected(char* kason_buf, const kason_lookup_table* table,
		kason_lookup_callback callback, void* user_data) {
	kason_lookup_context context;

	if (callback == NULL) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (table == NULL || table->slots == NULL || table->capacity <= 0 ||
			table->count <= 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	context.table = table;
	context.callback = callback;
	context.selector = NULL;
	context.converted_callback = NULL;
	context.user_data = user_data;
	return kason_parse_internal(kason_buf, NULL, NULL, NULL,
		M_AUTO, 0, TRUE, &context);
}

int kason_parse_range_selected_converted(char* begin, char* end,
		const kason_lookup_table* table,
		kason_lookup_conversion_selector selector,
		kason_lookup_converted_callback callback, void* user_data) {
	kason_lookup_context context;

	if (selector == NULL || callback == NULL) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (table == NULL || table->slots == NULL || table->capacity <= 0 ||
			table->count <= 0) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	context.table = table;
	context.callback = NULL;
	context.selector = selector;
	context.converted_callback = callback;
	context.user_data = user_data;
	return kason_parse_internal(begin, end, NULL, NULL,
		M_AUTO, 0, TRUE, &context);
}

int kason_parse_selected_converted(char* json, const kason_lookup_table* table,
		kason_lookup_conversion_selector selector,
		kason_lookup_converted_callback callback, void* user_data) {
	return kason_parse_range_selected_converted(json, NULL, table,
		selector, callback, user_data);
}

int kason_parse_deferred_containers(char* begin, char* end,
		kason_parse_callback callback, void* user_data) {
	return kason_parse_internal(begin, end, callback, user_data, M_AUTO, 0, FALSE, NULL);
}

int kason_parse_container(kason_data* container, kason_parse_callback callback, void* user_data) {
	if (callback == NULL) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (container == NULL || container->begin == NULL || container->end == NULL) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (container->type == KaSON_TYPE_ARRAY) {
		if (*container->begin == KaSON_CHAR_ARRAY_START &&
				*container->end == KaSON_CHAR_ARRAY_END) {
			if (container->begin + 1 == container->end) {
				return KaSON_PARSE_RESULT_SUCCESS;
			}
			return kason_parse_internal(container->begin + 1,
				container->end - 1, callback, user_data, M_ARRAY, 0,
				TRUE, NULL);
		}
		if (container->begin > container->end) {
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		return kason_parse_internal(container->begin, container->end,
				callback, user_data, M_ARRAY, 0, TRUE, NULL);
	}
	if (container->type == KaSON_TYPE_OBJECT) {
		return kason_parse_internal(container->begin, container->end,
				callback, user_data, M_AUTO, 0, TRUE, NULL);
	}
	return KaSON_PARSE_RESULT_ERROR;
}

int kason_parse_container_deferred(kason_data* container,
		kason_parse_callback callback, void* user_data, int depth) {
	if (callback == NULL) {
		return KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK;
	}
	if (container == NULL || container->begin == NULL || container->end == NULL) {
		return KaSON_PARSE_RESULT_ERROR;
	}
	if (container->type == KaSON_TYPE_ARRAY) {
		if (container->begin > container->end) {
			return KaSON_PARSE_RESULT_SUCCESS;
		}
		return kason_parse_internal(container->begin, container->end,
				callback, user_data, M_ARRAY, depth, FALSE, NULL);
	}
	if (container->type == KaSON_TYPE_OBJECT) {
		return kason_parse_internal(container->begin, container->end,
				callback, user_data, M_AUTO, depth, FALSE, NULL);
	}
	return KaSON_PARSE_RESULT_ERROR;
}

typedef struct s_kason_array {
	int max_elements;
	int current;
	kason_data* array;
} kason_array;

static int kason_parse_array_callback(kason_key* key, kason_data* data, int count, void* user_data) {
	kason_array* array = (kason_array*) user_data;
	(void)key;
	(void)count;
	if (array->current < array->max_elements) {
		array->array[array->current++] = *data;
	}
	return KaSON_CALLBACK_CONTINUE;
}

int kason_parse_array(char* begin, char* end, kason_data array[], const int max_elements) {
	if (max_elements < 0 || (max_elements > 0 && array == NULL)) {
		return -KaSON_PARSE_RESULT_ERROR;
	}
	kason_array a = {max_elements, 0, array};
	int err = kason_parse_internal(begin, end, kason_parse_array_callback, &a,
		M_ARRAY, 0, TRUE, NULL);
	if ( KaSON_PARSE_RESULT_SUCCESS == err ) {
		return a.current;
	} else {
		return -err;
	}
}

typedef struct s_kason_search_value {
	kason_key*  for_key;
	kason_data* out_value;
	int found;
} kason_search_value;

static int kason_get_value_callback(kason_key* key, kason_data* data, int count, void* user_data) {
	kason_search_value* sv = (kason_search_value*) user_data;
	(void)count;
	if (NULL == key) {
		return KaSON_CALLBACK_CONTINUE;
	}
	if (kason_strcmp(sv->for_key->begin, sv->for_key->end, key->begin, key->end) == 0) {
		*(sv->out_value) = *data;
		sv->found = 1;
		return KaSON_CALLBACK_BREAK;
	}
	return KaSON_CALLBACK_CONTINUE;
}

int kason_get_value(char* begin, char* end, kason_key* for_key, kason_data* out_value) {
	if (for_key == NULL || out_value == NULL) {
		return 0;
	}
	kason_search_value sv = {for_key, out_value, 0};
	int err = kason_parse_internal(begin, end, kason_get_value_callback, &sv,
		M_AUTO, 0, TRUE, NULL);
	if (KaSON_PARSE_RESULT_SUCCESS == err) {
		return sv.found;
	}
	return 0;
}

static int kason_integer_magnitude(const char* begin, const char* end,
		int* negative, uint64_t* magnitude) {
	const char* ptr = begin;
	uint64_t value = 0;

	if (begin == NULL || end == NULL || end < begin ||
			negative == NULL || magnitude == NULL) {
		return KaSON_CONVERT_ERROR;
	}
	*negative = FALSE;
	if (*ptr == '-') {
		*negative = TRUE;
		ptr++;
		if (ptr > end) {
			return KaSON_CONVERT_ERROR;
		}
	}
	if (*ptr == '0') {
		if (ptr != end) {
			return KaSON_CONVERT_ERROR;
		}
		*magnitude = 0;
		return KaSON_CONVERT_SUCCESS;
	}
	if (*ptr < '1' || *ptr > '9') {
		return KaSON_CONVERT_ERROR;
	}
	for (; ptr <= end; ptr++) {
		unsigned int digit;

		if (*ptr < '0' || *ptr > '9') {
			return KaSON_CONVERT_ERROR;
		}
		digit = (unsigned int)(*ptr - '0');
		if (value > (UINT64_MAX - digit) / 10U) {
			return KaSON_CONVERT_RANGE;
		}
		value = value * 10U + digit;
	}
	*magnitude = value;
	return KaSON_CONVERT_SUCCESS;
}

int kason_value_to_int64(int type, const char* begin, const char* end,
		int64_t* out_value) {
	uint64_t magnitude;
	uint64_t limit;
	int negative;
	int result;

	if (type != KaSON_TYPE_NUMBER) {
		return KaSON_CONVERT_TYPE_ERROR;
	}
	if (out_value == NULL) {
		return KaSON_CONVERT_ERROR;
	}
	result = kason_integer_magnitude(begin, end, &negative, &magnitude);
	if (result != KaSON_CONVERT_SUCCESS) {
		return result;
	}
	limit = negative ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;
	if (magnitude > limit) {
		return KaSON_CONVERT_RANGE;
	}
	if (negative) {
		*out_value = magnitude == (uint64_t)INT64_MAX + 1U
			? INT64_MIN
			: -(int64_t)magnitude;
	} else {
		*out_value = (int64_t)magnitude;
	}
	return KaSON_CONVERT_SUCCESS;
}

int kason_value_to_int(int type, const char* begin, const char* end,
		int* out_value) {
	int64_t value;
	int result;

	if (out_value == NULL) {
		return type == KaSON_TYPE_NUMBER
			? KaSON_CONVERT_ERROR
			: KaSON_CONVERT_TYPE_ERROR;
	}
	result = kason_value_to_int64(type, begin, end, &value);
	if (result != KaSON_CONVERT_SUCCESS) {
		return result;
	}
	if (value < INT_MIN || value > INT_MAX) {
		return KaSON_CONVERT_RANGE;
	}
	*out_value = (int)value;
	return KaSON_CONVERT_SUCCESS;
}

int kason_value_to_uint64(int type, const char* begin, const char* end,
		uint64_t* out_value) {
	uint64_t magnitude;
	int negative;
	int result;

	if (type != KaSON_TYPE_NUMBER) {
		return KaSON_CONVERT_TYPE_ERROR;
	}
	if (out_value == NULL) {
		return KaSON_CONVERT_ERROR;
	}
	result = kason_integer_magnitude(begin, end, &negative, &magnitude);
	if (result != KaSON_CONVERT_SUCCESS) {
		return result;
	}
	if (negative) {
		return KaSON_CONVERT_RANGE;
	}
	*out_value = magnitude;
	return KaSON_CONVERT_SUCCESS;
}

int kason_value_to_double(int type, const char* begin, const char* end,
		double* out_value) {
	const char* ptr;
	kason_number_state state;
	kason_number_accumulator accumulator;
	kason_cached_number cached;

	if (type != KaSON_TYPE_NUMBER) {
		return KaSON_CONVERT_TYPE_ERROR;
	}
	if (begin == NULL || end == NULL || end < begin || out_value == NULL) {
		return KaSON_CONVERT_ERROR;
	}
	state = kason_number_begin(*begin);
	if (state == KaSON_NUMBER_INVALID) {
		return KaSON_CONVERT_ERROR;
	}
	kason_number_accumulator_init(&accumulator, KaSON_CACHED_NUMBER_DOUBLE);
	kason_number_accumulator_feed(&accumulator, *begin, state);
	for (ptr = begin + 1; ptr <= end; ptr++) {
		if (!is_digit(*ptr) ||
				(state != KaSON_NUMBER_INTEGER &&
				state != KaSON_NUMBER_FRACTION &&
				state != KaSON_NUMBER_EXPONENT)) {
			state = kason_number_next(state, *ptr);
		}
		if (state == KaSON_NUMBER_INVALID) {
			return KaSON_CONVERT_ERROR;
		}
		kason_number_accumulator_feed(&accumulator, *ptr, state);
	}
	if (!kason_number_complete(state)) {
		return KaSON_CONVERT_ERROR;
	}
	kason_number_accumulator_finish(&accumulator, &cached);
	if (cached.result != KaSON_CONVERT_SUCCESS) {
		return cached.result;
	}
	*out_value = cached.value.real_value;
	return KaSON_CONVERT_SUCCESS;
}

int kason_strlen(const char* kason_string_begin, const char* kason_string_end) {
	return kason_strcpy(kason_string_begin, kason_string_end, NULL, 0);
}

/*
 * Copy a decoded JSON string into a UTF-8 buffer.
 */
int kason_strcpy(const char* kason_string_begin, const char* kason_string_end, char* buffer, int buffer_size) {
	const char* ptr = kason_string_begin;
	uint32_t value;
	int written = 0;
	int full = FALSE;
	int result;

	if (buffer_size < 0) {
		return KaSON_STRING_RESULT_ERROR;
	}
	while ((result = kason_string_next(&ptr, kason_string_end, &value)) == KaSON_STRING_NEXT_VALUE) {
		char encoded[4];
		int length = kason_utf8_encode(value, encoded);
		int i;

		if (NULL == buffer) {
			written += length;
		} else if (!full && length <= buffer_size - written) {
			for (i = 0; i < length; i++) {
				buffer[written + i] = encoded[i];
			}
			written += length;
		} else {
			full = TRUE;
		}
	}
	return result == KaSON_STRING_NEXT_END ? written : KaSON_STRING_RESULT_ERROR;
}

int kason_strcpy_utf16(const char* kason_string_begin, const char* kason_string_end, uint16_t* buffer, int buffer_size) {
	const char* ptr = kason_string_begin;
	uint32_t value;
	int written = 0;
	int full = FALSE;
	int result;

	if (buffer_size < 0) {
		return KaSON_STRING_RESULT_ERROR;
	}
	while ((result = kason_string_next(&ptr, kason_string_end, &value)) == KaSON_STRING_NEXT_VALUE) {
		int length = value <= 0xffff ? 1 : 2;

		if (NULL == buffer) {
			written += length;
		} else if (!full && length <= buffer_size - written) {
			if (length == 1) {
				buffer[written] = (uint16_t)value;
			} else {
				uint32_t supplementary = value - 0x10000;
				buffer[written] = (uint16_t)(0xd800 + (supplementary >> 10));
				buffer[written + 1] = (uint16_t)(0xdc00 + (supplementary & 0x3ff));
			}
			written += length;
		} else {
			full = TRUE;
		}
	}
	return result == KaSON_STRING_NEXT_END ? written : KaSON_STRING_RESULT_ERROR;
}

int kason_strcpy_utf32(const char* kason_string_begin, const char* kason_string_end, uint32_t* buffer, int buffer_size) {
	const char* ptr = kason_string_begin;
	uint32_t value;
	int written = 0;
	int full = FALSE;
	int result;

	if (buffer_size < 0) {
		return KaSON_STRING_RESULT_ERROR;
	}
	while ((result = kason_string_next(&ptr, kason_string_end, &value)) == KaSON_STRING_NEXT_VALUE) {
		if (NULL == buffer) {
			written++;
		} else if (!full && written < buffer_size) {
			buffer[written++] = value;
		} else {
			full = TRUE;
		}
	}
	return result == KaSON_STRING_NEXT_END ? written : KaSON_STRING_RESULT_ERROR;
}

int kason_strcmp(const char* kason_string1_begin, const char* kason_string1_end, const char* kason_string2_begin, const char* kason_string2_end) {
	const char* ptr1 = kason_string1_begin;
	const char* ptr2 = kason_string2_begin;
	uint32_t value1;
	uint32_t value2;
	int result1;
	int result2;

	for (;;) {
		result1 = kason_string_next_internal(&ptr1, kason_string1_end, &value1, FALSE);
		result2 = kason_string_next_internal(&ptr2, kason_string2_end, &value2, FALSE);
		if (result1 == KaSON_STRING_NEXT_ERROR || result2 == KaSON_STRING_NEXT_ERROR) {
			if (result1 == result2) {
				return 0;
			}
			return result1 == KaSON_STRING_NEXT_ERROR ? -1 : 1;
		}
		if (result1 == KaSON_STRING_NEXT_END || result2 == KaSON_STRING_NEXT_END) {
			if (result1 == result2) {
				return 0;
			}
			return result1 == KaSON_STRING_NEXT_END ? -1 : 1;
		}
		if (value1 < value2) {
			return -1;
		}
		if (value1 > value2) {
			return 1;
		}
	}
}
