#ifndef KaSON_INTERNAL_H
#define KaSON_INTERNAL_H

#include "kason.h"

#define KaSON_CACHED_NUMBER_NONE     (0)
#define KaSON_CACHED_NUMBER_SIGNED   (1)
#define KaSON_CACHED_NUMBER_UNSIGNED (2)
#define KaSON_CACHED_NUMBER_DOUBLE   (3)

typedef struct s_kason_cached_number {
	int kind;
	int result;
	union {
		int64_t signed_value;
		uint64_t unsigned_value;
		double real_value;
	} value;
} kason_cached_number;

typedef int (*kason_lookup_conversion_selector)(
		const kason_lookup_key* matched_key, void* user_data);
typedef int (*kason_lookup_converted_callback)(
		const kason_lookup_key* matched_key, kason_data* data, int count,
		const kason_cached_number* cached_number, void* user_data);

int kason_parse_deferred_containers(char* begin, char* end,
		kason_parse_callback callback, void* user_data);
int kason_parse_container_deferred(kason_data* container,
		kason_parse_callback callback, void* user_data, int depth);

int kason_parse_selected_converted(char* json, const kason_lookup_table* table,
		kason_lookup_conversion_selector selector,
		kason_lookup_converted_callback callback, void* user_data);
int kason_parse_range_selected_converted(char* begin, char* end,
		const kason_lookup_table* table,
		kason_lookup_conversion_selector selector,
		kason_lookup_converted_callback callback, void* user_data);

#endif
