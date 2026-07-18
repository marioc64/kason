#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kason.h"
#include "kason_schema.h"

typedef struct {
    char a[16];
    int b;
    double c;
    uint16_t u16[8];
    uint32_t u32[8];
    uint32_t values[4];
    size_t value_count;
} fuzz_schema_value;

static const uint16_t fuzz_u16_default[] = {0};
static const uint32_t fuzz_u32_default[] = {0};

static const kason_schema_field fuzz_schema_fields[] = {
    KaSON_FIELD_STRING(fuzz_schema_value, a, "a", KaSON_DEFAULT_STRING("")),
    KaSON_FIELD_INT(fuzz_schema_value, b, "b", KaSON_DEFAULT_INT(0)),
    KaSON_FIELD_DOUBLE(fuzz_schema_value, c, "c", KaSON_DEFAULT_DOUBLE(0.0)),
    KaSON_FIELD_STRING_U16(fuzz_schema_value, u16, "u16",
                          KaSON_DEFAULT_STRING_U16(fuzz_u16_default)),
    KaSON_FIELD_STRING_U32(fuzz_schema_value, u32, "u32",
                          KaSON_DEFAULT_STRING_U32(fuzz_u32_default)),
    KaSON_FIELD_U32_ARRAY(fuzz_schema_value, values, value_count, "values",
                         KaSON_DEFAULT_EMPTY_ARRAY)
};

KaSON_SCHEMA_DEFINE(fuzz_schema, fuzz_schema_value, fuzz_schema_fields, 8);

static void exercise_string(const char *begin, const char *end)
{
    char utf8[32];
    uint16_t utf16[16];
    uint32_t utf32[8];

    (void)kason_strlen(begin, end);
    (void)kason_strcpy(begin, end, utf8, (int)sizeof(utf8));
    (void)kason_strcpy_utf16(begin, end, utf16,
                            (int)(sizeof(utf16) / sizeof(utf16[0])));
    (void)kason_strcpy_utf32(begin, end, utf32,
                            (int)(sizeof(utf32) / sizeof(utf32[0])));
    (void)kason_strcmp(begin, end, begin, end);
}

static void exercise_primitive(int type, const char *begin, const char *end)
{
    int int_value;
    int64_t int64_value;
    uint64_t uint64_value;
    double double_value;

    (void)kason_value_to_int(type, begin, end, &int_value);
    (void)kason_value_to_int64(type, begin, end, &int64_value);
    (void)kason_value_to_uint64(type, begin, end, &uint64_value);
    (void)kason_value_to_double(type, begin, end, &double_value);
}

static int accept_value(kason_key *key, kason_data *data, int count,
                        void *user_data)
{
    (void)count;
    (void)user_data;
    if (key != NULL) {
        exercise_string(key->begin, key->end);
    }
    if (data->type == KaSON_TYPE_STRING) {
        exercise_string(data->begin, data->end);
    } else {
        exercise_primitive(data->type, data->begin, data->end);
    }
    return KaSON_CALLBACK_CONTINUE;
}

static int accept_selected_value(const kason_lookup_key *key, kason_data *data,
                                 int count, void *user_data)
{
    (void)key;
    return accept_value(NULL, data, count, user_data);
}

static int accept_stream_value(kason_key *key, kason_stream_data *data,
                               void *user_data)
{
    (void)user_data;
    if (key != NULL) {
        exercise_string(key->begin, key->end);
    }
    if (data->event == KaSON_STREAM_EVENT_VALUE &&
            data->type == KaSON_TYPE_STRING) {
        exercise_string(data->begin, data->end);
    } else if (data->event == KaSON_STREAM_EVENT_VALUE) {
        exercise_primitive(data->type, data->begin, data->end);
    }
    return KaSON_CALLBACK_CONTINUE;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    kason_stream stream;
    char stream_scratch[512];
    kason_lookup_key selected_slots[2];
    kason_lookup_table selected_table;
    char *input;
    size_t offset;
    int selected_ready;
    static int schema_ready;

    if (size > (size_t)INT32_MAX) {
        return 0;
    }

    input = (char *)malloc(size + 1);
    if (input == NULL) {
        return 0;
    }
    if (size > 0) {
        memcpy(input, data, size);
    }
    input[size] = '\0';

    if (!schema_ready) {
        schema_ready = kason_schema_init(&fuzz_schema) == KaSON_SCHEMA_SUCCESS;
    }
    if (schema_ready) {
        fuzz_schema_value schema_value;
        kason_writer writer;
        if (kason_unpack(input, &fuzz_schema, &schema_value, NULL) ==
                KaSON_SCHEMA_SUCCESS &&
                kason_writer_init_counter(&writer) == KaSON_SCHEMA_SUCCESS) {
            (void)kason_pack(&writer, &fuzz_schema, &schema_value,
                            KaSON_PACK_OMIT_DEFAULTS, NULL);
        }
    }

    (void)kason_parse(input, accept_value, NULL);
    selected_ready = kason_lookup_table_init(&selected_table, selected_slots, 2) ==
            KaSON_PARSE_RESULT_SUCCESS &&
        kason_lookup_table_add(&selected_table, "a", 1) ==
            KaSON_PARSE_RESULT_SUCCESS;
    if (selected_ready) {
        (void)kason_parse_selected(input, &selected_table,
                                  accept_selected_value, NULL);
    }
    if (size > 0) {
        (void)kason_parse_range(input, input + size - 1, accept_value, NULL);
        if (selected_ready) {
            (void)kason_parse_range_selected(input, input + size - 1,
                                            &selected_table,
                                            accept_selected_value, NULL);
        }
    }

    if (kason_stream_init(&stream, stream_scratch,
                         (int)sizeof(stream_scratch),
                         accept_stream_value, NULL) ==
            KaSON_PARSE_RESULT_SUCCESS) {
        offset = 0;
        while (offset < size) {
            size_t chunk_size = (size_t)(data[offset] & 31u) + 1;
            int result;

            if (chunk_size > size - offset) {
                chunk_size = size - offset;
            }
            result = kason_stream_feed(&stream, input + offset,
                                      (int)chunk_size);
            offset += chunk_size;
            if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) != 0 ||
                    result == KaSON_PARSE_RESULT_SUCCESS) {
                break;
            }
        }
        (void)kason_stream_finish(&stream);
    }

    free(input);
    return 0;
}
