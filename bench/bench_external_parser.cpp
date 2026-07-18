#include "bench_external.h"
#include "bench_lookup.h"
#include "bench_struct.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(KaSON_BENCH_JSON_C)
#include <json_object.h>
#include <json_tokener.h>
#elif defined(KaSON_BENCH_JANSSON)
#include <jansson.h>
#elif defined(KaSON_BENCH_YYJSON)
#include <yyjson.h>
#elif defined(KaSON_BENCH_JSMN)
#include <jsmn.h>
#elif defined(KaSON_BENCH_PARSON)
#include <parson.h>
#else
#error "An external benchmark parser must be selected"
#endif

#if defined(KaSON_BENCH_JSON_C)

static json_object *load_document(const char *json, size_t length)
{
    json_tokener *tokener = json_tokener_new();
    json_object *root;

    if (tokener == NULL) {
        return NULL;
    }
    root = json_tokener_parse_ex(tokener, json, (int)length);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    json_tokener_free(tokener);
    if (error != json_tokener_success || root == NULL) {
        if (root != NULL) {
            json_object_put(root);
        }
        return NULL;
    }
    return root;
}

const char *benchmark_parser_name()
{
    return "json-c";
}

void *benchmark_hold(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    json_object *root = load_document(json, length);
    if (root != NULL) {
        *sink += (unsigned long long)json_object_get_type(root);
    }
    return root;
}

void benchmark_release(void *hold)
{
    json_object_put((json_object *)hold);
}

bool benchmark_parse(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    json_object *root = load_document(json, length);
    if (root == NULL) {
        return false;
    }
    *sink += (unsigned long long)json_object_get_type(root);
    json_object_put(root);
    return true;
}

bool benchmark_lookup(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    json_object *root = load_document(json, length);
    unsigned long long sum = 0;
    if (root == NULL) {
        return false;
    }
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        json_object *value = NULL;
        if (!json_object_object_get_ex(root, benchmark_lookup_keys[i], &value) ||
                json_object_get_type(value) != json_type_int ||
                (unsigned long long)json_object_get_int64(value) !=
                    benchmark_lookup_values[i]) {
            json_object_put(root);
            return false;
        }
        sum += (unsigned long long)json_object_get_int64(value);
    }
    *sink += sum;
    json_object_put(root);
    return true;
}

bool benchmark_struct(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    json_object *root = load_document(json, length);
    json_object *name = NULL;
    json_object *id = NULL;
    json_object *offset = NULL;
    json_object *ratio = NULL;
    BenchmarkRecord record = {};
    bool valid;

    if (root == NULL) {
        return false;
    }
    valid = json_object_is_type(root, json_type_object) &&
        json_object_object_get_ex(root, benchmark_record_name_key, &name) &&
        json_object_object_get_ex(root, benchmark_record_id_key, &id) &&
        json_object_object_get_ex(root, benchmark_record_offset_key, &offset) &&
        json_object_object_get_ex(root, benchmark_record_ratio_key, &ratio) &&
        json_object_is_type(name, json_type_string) &&
        json_object_is_type(id, json_type_int) &&
        json_object_is_type(offset, json_type_int) &&
        json_object_is_type(ratio, json_type_double) &&
        benchmark_record_set_name(record,
                                  json_object_get_string(name),
                                  (std::size_t)json_object_get_string_len(name));
    if (valid) {
        std::uint64_t id_value = json_object_get_uint64(id);
        std::int64_t offset_value = json_object_get_int64(offset);
        valid = id_value <= UINT32_MAX && offset_value >= INT_MIN &&
            offset_value <= INT_MAX;
        if (valid) {
            record.id = (std::uint32_t)id_value;
            record.offset = (int)offset_value;
            record.ratio = json_object_get_double(ratio);
        }
    }
    valid = valid && benchmark_record_valid(record);
    if (valid) {
        *sink += benchmark_record_checksum(record);
    }
    json_object_put(root);
    return valid;
}

#elif defined(KaSON_BENCH_JANSSON)

const char *benchmark_parser_name()
{
    return "jansson";
}

void *benchmark_hold(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    json_error_t error;
    json_t *root = json_loadb(json, length, 0, &error);
    if (root != NULL) {
        *sink += (unsigned long long)json_typeof(root);
    }
    return root;
}

void benchmark_release(void *hold)
{
    json_decref((json_t *)hold);
}

bool benchmark_parse(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    json_error_t error;
    json_t *root = json_loadb(json, length, 0, &error);
    if (root == NULL) {
        return false;
    }
    *sink += (unsigned long long)json_typeof(root);
    json_decref(root);
    return true;
}

bool benchmark_lookup(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    json_error_t error;
    json_t *root = json_loadb(json, length, 0, &error);
    unsigned long long sum = 0;
    if (root == NULL) {
        return false;
    }
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        json_t *value = json_object_get(root, benchmark_lookup_keys[i]);
        if (!json_is_integer(value) ||
                (unsigned long long)json_integer_value(value) !=
                    benchmark_lookup_values[i]) {
            json_decref(root);
            return false;
        }
        sum += (unsigned long long)json_integer_value(value);
    }
    *sink += sum;
    json_decref(root);
    return true;
}

bool benchmark_struct(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    json_error_t error;
    json_t *root = json_loadb(json, length, 0, &error);
    BenchmarkRecord record = {};
    bool valid;

    if (root == NULL) {
        return false;
    }
    json_t *name = json_object_get(root, benchmark_record_name_key);
    json_t *id = json_object_get(root, benchmark_record_id_key);
    json_t *offset = json_object_get(root, benchmark_record_offset_key);
    json_t *ratio = json_object_get(root, benchmark_record_ratio_key);
    valid = json_is_object(root) && json_is_string(name) &&
        json_is_integer(id) && json_is_integer(offset) &&
        json_is_number(ratio) &&
        benchmark_record_set_name(record,
                                  json_string_value(name),
                                  json_string_length(name));
    if (valid) {
        json_int_t id_value = json_integer_value(id);
        json_int_t offset_value = json_integer_value(offset);
        valid = id_value >= 0 && (std::uint64_t)id_value <= UINT32_MAX &&
            offset_value >= INT_MIN && offset_value <= INT_MAX;
        if (valid) {
            record.id = (std::uint32_t)id_value;
            record.offset = (int)offset_value;
            record.ratio = json_number_value(ratio);
        }
    }
    valid = valid && benchmark_record_valid(record);
    if (valid) {
        *sink += benchmark_record_checksum(record);
    }
    json_decref(root);
    return valid;
}

#elif defined(KaSON_BENCH_YYJSON)

const char *benchmark_parser_name()
{
    return "yyjson";
}

void *benchmark_hold(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    yyjson_doc *doc = yyjson_read(json, length, 0);
    if (doc != NULL) {
        *sink += (unsigned long long)yyjson_get_type(yyjson_doc_get_root(doc));
    }
    return doc;
}

void benchmark_release(void *hold)
{
    yyjson_doc_free((yyjson_doc *)hold);
}

bool benchmark_parse(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    yyjson_doc *doc = yyjson_read(json, length, 0);
    if (doc == NULL) {
        return false;
    }
    *sink += (unsigned long long)yyjson_get_type(yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return true;
}

bool benchmark_lookup(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    yyjson_doc *doc = yyjson_read(json, length, 0);
    yyjson_val *root;
    unsigned long long sum = 0;
    if (doc == NULL) {
        return false;
    }
    root = yyjson_doc_get_root(doc);
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        yyjson_val *value = yyjson_obj_get(root, benchmark_lookup_keys[i]);
        if (!yyjson_is_int(value) ||
                yyjson_get_uint(value) != benchmark_lookup_values[i]) {
            yyjson_doc_free(doc);
            return false;
        }
        sum += yyjson_get_uint(value);
    }
    *sink += sum;
    yyjson_doc_free(doc);
    return true;
}

bool benchmark_struct(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    yyjson_doc *doc = yyjson_read(json, length, 0);
    BenchmarkRecord record = {};
    bool valid;

    if (doc == NULL) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *name = yyjson_obj_get(root, benchmark_record_name_key);
    yyjson_val *id = yyjson_obj_get(root, benchmark_record_id_key);
    yyjson_val *offset = yyjson_obj_get(root, benchmark_record_offset_key);
    yyjson_val *ratio = yyjson_obj_get(root, benchmark_record_ratio_key);
    valid = yyjson_is_obj(root) && yyjson_is_str(name) && yyjson_is_uint(id) &&
        yyjson_is_sint(offset) && yyjson_is_num(ratio) &&
        benchmark_record_set_name(record, yyjson_get_str(name),
                                  yyjson_get_len(name));
    if (valid) {
        std::uint64_t id_value = yyjson_get_uint(id);
        std::int64_t offset_value = yyjson_get_sint(offset);
        valid = id_value <= UINT32_MAX && offset_value >= INT_MIN &&
            offset_value <= INT_MAX;
        if (valid) {
            record.id = (std::uint32_t)id_value;
            record.offset = (int)offset_value;
            record.ratio = yyjson_get_num(ratio);
        }
    }
    valid = valid && benchmark_record_valid(record);
    if (valid) {
        *sink += benchmark_record_checksum(record);
    }
    yyjson_doc_free(doc);
    return valid;
}

#elif defined(KaSON_BENCH_JSMN)

struct JsmnHold {
    std::vector<jsmntok_t> tokens;
};

static bool parse_into(const char *json,
                       size_t length,
                       std::vector<jsmntok_t> &tokens,
                       volatile unsigned long long *sink)
{
    jsmn_parser parser;

    if (tokens.empty()) {
        jsmn_init(&parser);
        int token_count = jsmn_parse(&parser, json, length, NULL, 0);
        if (token_count <= 0) {
            return false;
        }
        tokens.resize((size_t)token_count);
    }

    jsmn_init(&parser);
    int parsed = jsmn_parse(&parser,
                            json,
                            length,
                            tokens.data(),
                            (unsigned int)tokens.size());
    if (parsed <= 0) {
        return false;
    }
    *sink += (unsigned long long)tokens[0].type + (unsigned long long)parsed;
    return true;
}

const char *benchmark_parser_name()
{
    return "jsmn";
}

void *benchmark_hold(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    JsmnHold *hold = new JsmnHold;
    if (!parse_into(json, length, hold->tokens, sink)) {
        delete hold;
        return NULL;
    }
    return hold;
}

void benchmark_release(void *hold)
{
    delete (JsmnHold *)hold;
}

bool benchmark_parse(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    static thread_local std::vector<jsmntok_t> tokens;
    return parse_into(json, length, tokens, sink);
}

bool benchmark_lookup(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    static thread_local std::vector<jsmntok_t> tokens;
    unsigned int found = 0;
    unsigned long long sum = 0;
    if (!parse_into(json, length, tokens, sink)) {
        return false;
    }
    for (std::size_t token_index = 1;
         token_index + 1 < tokens.size();
         token_index += 2) {
        const jsmntok_t &key = tokens[token_index];
        const jsmntok_t &value = tokens[token_index + 1];
        if (key.type != JSMN_STRING || value.type != JSMN_PRIMITIVE) {
            return false;
        }
        for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
            std::size_t key_length = benchmark_lookup_lengths[i];
            if ((std::size_t)(key.end - key.start) == key_length &&
                    std::memcmp(json + key.start,
                                benchmark_lookup_keys[i],
                                key_length) == 0) {
                unsigned long long parsed_value = 0;
                for (int pos = value.start; pos < value.end; ++pos) {
                    if (json[pos] < '0' || json[pos] > '9') {
                        return false;
                    }
                    parsed_value = parsed_value * 10U +
                        (unsigned long long)(json[pos] - '0');
                }
                if (parsed_value != benchmark_lookup_values[i]) {
                    return false;
                }
                found |= 1U << i;
                sum += parsed_value;
                break;
            }
        }
    }
    if (found != benchmark_lookup_all_found) {
        return false;
    }
    *sink += sum;
    return true;
}

static bool jsmn_token_equals(const char *json, const jsmntok_t &token,
                              const char *expected, std::size_t length)
{
    return token.type == JSMN_STRING &&
        (std::size_t)(token.end - token.start) == length &&
        std::memcmp(json + token.start, expected, length) == 0;
}

static bool jsmn_token_text(const char *json, const jsmntok_t &token,
                            char *buffer, std::size_t capacity)
{
    std::size_t length = (std::size_t)(token.end - token.start);
    if (length >= capacity) {
        return false;
    }
    std::memcpy(buffer, json + token.start, length);
    buffer[length] = '\0';
    return true;
}

bool benchmark_struct(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    static thread_local std::vector<jsmntok_t> tokens;
    BenchmarkRecord record = {};
    unsigned int found = 0;

    if (!parse_into(json, length, tokens, sink) || tokens.empty() ||
            tokens[0].type != JSMN_OBJECT) {
        return false;
    }
    for (std::size_t token_index = 1;
         token_index + 1 < tokens.size(); token_index += 2) {
        const jsmntok_t &key = tokens[token_index];
        const jsmntok_t &value = tokens[token_index + 1];
        unsigned int field;

        if (jsmn_token_equals(json, key, benchmark_record_name_key,
                              sizeof(benchmark_record_name_key) - 1)) {
            field = benchmark_struct_found_name;
            if (value.type != JSMN_STRING ||
                    !jsmn_token_text(json, value, record.name,
                                     sizeof(record.name))) {
                return false;
            }
        } else if (jsmn_token_equals(json, key, benchmark_record_id_key,
                                     sizeof(benchmark_record_id_key) - 1)) {
            char text[32];
            char *end;
            unsigned long long converted;
            field = benchmark_struct_found_id;
            if (value.type != JSMN_PRIMITIVE ||
                    !jsmn_token_text(json, value, text, sizeof(text))) {
                return false;
            }
            errno = 0;
            converted = std::strtoull(text, &end, 10);
            if (errno != 0 || text[0] == '-' || *end != '\0' ||
                    converted > UINT32_MAX) {
                return false;
            }
            record.id = (std::uint32_t)converted;
        } else if (jsmn_token_equals(json, key, benchmark_record_offset_key,
                                     sizeof(benchmark_record_offset_key) - 1)) {
            char text[32];
            char *end;
            long converted;
            field = benchmark_struct_found_offset;
            if (value.type != JSMN_PRIMITIVE ||
                    !jsmn_token_text(json, value, text, sizeof(text))) {
                return false;
            }
            errno = 0;
            converted = std::strtol(text, &end, 10);
            if (errno != 0 || *end != '\0' || converted < INT_MIN ||
                    converted > INT_MAX) {
                return false;
            }
            record.offset = (int)converted;
        } else if (jsmn_token_equals(json, key, benchmark_record_ratio_key,
                                     sizeof(benchmark_record_ratio_key) - 1)) {
            char text[64];
            char *end;
            field = benchmark_struct_found_ratio;
            if (value.type != JSMN_PRIMITIVE ||
                    !jsmn_token_text(json, value, text, sizeof(text))) {
                return false;
            }
            errno = 0;
            record.ratio = std::strtod(text, &end);
            if (errno != 0 || *end != '\0') {
                return false;
            }
        } else {
            continue;
        }

        if ((found & field) != 0) {
            return false;
        }
        found |= field;
    }
    if (found != benchmark_struct_found_all || !benchmark_record_valid(record)) {
        return false;
    }
    *sink += benchmark_record_checksum(record);
    return true;
}

bool benchmark_struct64(const char *json,
                        size_t length,
                        volatile unsigned long long *sink)
{
    static thread_local std::vector<jsmntok_t> tokens;
    BenchmarkLargeRecord record = {};
    std::uint64_t found = 0;

    if (!parse_into(json, length, tokens, sink) || tokens.empty() ||
            tokens[0].type != JSMN_OBJECT) {
        return false;
    }
    for (std::size_t token_index = 1;
         token_index + 1 < tokens.size(); token_index += 2) {
        const jsmntok_t &key = tokens[token_index];
        const jsmntok_t &value = tokens[token_index + 1];
        std::size_t key_length;
        std::size_t field_index = 0;
        std::uint64_t parsed_value = 0;

        if (key.type != JSMN_STRING || value.type != JSMN_PRIMITIVE) {
            return false;
        }
        key_length = (std::size_t)(key.end - key.start);
        if (key_length < 6 ||
                std::memcmp(json + key.start, "field", 5) != 0) {
            continue;
        }
        for (std::size_t pos = 5; pos < key_length; ++pos) {
            char digit = json[key.start + (int)pos];
            if (digit < '0' || digit > '9') {
                return false;
            }
            field_index = field_index * 10U + (std::size_t)(digit - '0');
        }
        if (field_index >= benchmark_large_field_count ||
                (found & (UINT64_C(1) << field_index)) != 0) {
            return false;
        }
        for (int pos = value.start; pos < value.end; ++pos) {
            if (json[pos] < '0' || json[pos] > '9') {
                return false;
            }
            parsed_value = parsed_value * 10U +
                (std::uint64_t)(json[pos] - '0');
        }
        if (parsed_value > UINT32_MAX) {
            return false;
        }
        record.values[field_index] = (std::uint32_t)parsed_value;
        found |= UINT64_C(1) << field_index;
    }
    if (found != UINT64_MAX || !benchmark_large_record_valid(record)) {
        return false;
    }
    *sink += benchmark_large_record_checksum(record);
    return true;
}

#elif defined(KaSON_BENCH_PARSON)

const char *benchmark_parser_name()
{
    return "parson";
}

void *benchmark_hold(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    (void)length;
    JSON_Value *root = json_parse_string(json);
    if (root != NULL) {
        *sink += (unsigned long long)json_value_get_type(root);
    }
    return root;
}

void benchmark_release(void *hold)
{
    json_value_free((JSON_Value *)hold);
}

bool benchmark_parse(const char *json,
                     size_t length,
                     volatile unsigned long long *sink)
{
    (void)length;
    JSON_Value *root = json_parse_string(json);
    if (root == NULL) {
        return false;
    }
    *sink += (unsigned long long)json_value_get_type(root);
    json_value_free(root);
    return true;
}

bool benchmark_lookup(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    (void)length;
    JSON_Value *root = json_parse_string(json);
    JSON_Object *object;
    unsigned long long sum = 0;
    if (root == NULL) {
        return false;
    }
    object = json_value_get_object(root);
    if (object == NULL) {
        json_value_free(root);
        return false;
    }
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        JSON_Value *value = json_object_get_value(object, benchmark_lookup_keys[i]);
        if (json_value_get_type(value) != JSONNumber ||
                (unsigned long long)json_value_get_number(value) !=
                    benchmark_lookup_values[i]) {
            json_value_free(root);
            return false;
        }
        sum += (unsigned long long)json_value_get_number(value);
    }
    *sink += sum;
    json_value_free(root);
    return true;
}

bool benchmark_struct(const char *json,
                      size_t length,
                      volatile unsigned long long *sink)
{
    (void)length;
    JSON_Value *root = json_parse_string(json);
    BenchmarkRecord record = {};
    bool valid;

    if (root == NULL) {
        return false;
    }
    JSON_Object *object = json_value_get_object(root);
    const char *name = object == NULL ? NULL :
        json_object_get_string(object, benchmark_record_name_key);
    JSON_Value *id = object == NULL ? NULL :
        json_object_get_value(object, benchmark_record_id_key);
    JSON_Value *offset = object == NULL ? NULL :
        json_object_get_value(object, benchmark_record_offset_key);
    JSON_Value *ratio = object == NULL ? NULL :
        json_object_get_value(object, benchmark_record_ratio_key);
    valid = object != NULL && name != NULL &&
        json_value_get_type(id) == JSONNumber &&
        json_value_get_type(offset) == JSONNumber &&
        json_value_get_type(ratio) == JSONNumber &&
        benchmark_record_set_name(record, name, std::strlen(name));
    if (valid) {
        double id_value = json_value_get_number(id);
        double offset_value = json_value_get_number(offset);
        valid = id_value >= 0.0 && id_value <= UINT32_MAX &&
            offset_value >= INT_MIN && offset_value <= INT_MAX;
        if (valid) {
            record.id = (std::uint32_t)id_value;
            record.offset = (int)offset_value;
            record.ratio = json_value_get_number(ratio);
            valid = (double)record.id == id_value &&
                (double)record.offset == offset_value;
        }
    }
    valid = valid && benchmark_record_valid(record);
    if (valid) {
        *sink += benchmark_record_checksum(record);
    }
    json_value_free(root);
    return valid;
}

#endif

#if !defined(KaSON_BENCH_JSMN)
bool benchmark_struct64(const char *json,
                        size_t length,
                        volatile unsigned long long *sink)
{
    (void)json;
    (void)length;
    (void)sink;
    return false;
}
#endif
