#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(KaSON_BENCH_CJSON)
#include <cjson/cJSON.h>
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
#include <json-glib/json-glib.h>
#endif
#if defined(KaSON_BENCH_SIMDJSON)
#include <simdjson.h>
#endif

#include "bench_lookup.h"
#include "bench_rss.h"
#include "bench_struct.h"

extern "C" {
#include "kason.h"
#include "kason_schema.h"
}

static volatile unsigned long long bench_sink;

struct BenchAccumulator {
    unsigned long long value;
};

static double now_seconds()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    std::chrono::duration<double> elapsed = clock::now() - start;
    return elapsed.count();
}

static std::string make_flat_json()
{
    std::string out;
    out.reserve(900000);
    out.push_back('{');
    for (int i = 0; i < 50000; ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += "\"k";
        out += std::to_string(i);
        out += "\":";
        out += std::to_string(i);
    }
    out.push_back('}');
    return out;
}

static std::string make_nested_json()
{
    std::string out;
    out.reserve(900000);
    out.push_back('{');
    for (int object = 0; object < 256; ++object) {
        if (object > 0) {
            out.push_back(',');
        }
        out += "\"o";
        out += std::to_string(object);
        out += "\":{";
        for (int prop = 0; prop < 256; ++prop) {
            if (prop > 0) {
                out.push_back(',');
            }
            out += "\"p";
            out += std::to_string(prop);
            out += "\":";
            out += std::to_string(object + prop);
        }
        out.push_back('}');
    }
    out.push_back('}');
    return out;
}

static int parse_callback(kason_key *key, kason_data *data, int count, void *user_data)
{
    BenchAccumulator *accumulator = (BenchAccumulator *)user_data;
    accumulator->value += (unsigned long long)data->type + (unsigned long long)count;
    if (key != NULL && key->end >= key->begin) {
        accumulator->value += (unsigned long long)(key->end - key->begin + 1);
    }
    if (data->end >= data->begin) {
        accumulator->value += (unsigned long long)(data->end - data->begin + 1);
    }
    return KaSON_CALLBACK_CONTINUE;
}

static bool parse_kason_range(const std::string &json)
{
    char *begin = const_cast<char *>(json.data());
    char *end = begin + json.size() - 1;
    BenchAccumulator accumulator = {0};
    bool success = kason_parse_range(begin, end, parse_callback, &accumulator) ==
        KaSON_PARSE_RESULT_SUCCESS;
    bench_sink += accumulator.value;
    return success;
}

struct LookupResult {
    unsigned int found;
    unsigned long long sum;
};

static const kason_lookup_table *prepared_lookup_table()
{
    static kason_lookup_key slots[benchmark_lookup_count * 2];
    static kason_lookup_table table;
    static bool initialized;

    if (!initialized) {
        if (kason_lookup_table_init(&table, slots,
                                   (int)(benchmark_lookup_count * 2)) !=
                KaSON_PARSE_RESULT_SUCCESS) {
            return nullptr;
        }
        for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
            if (kason_lookup_table_add(&table, benchmark_lookup_keys[i],
                                      (int)benchmark_lookup_lengths[i]) !=
                    KaSON_PARSE_RESULT_SUCCESS) {
                return nullptr;
            }
        }
        initialized = true;
    }
    return &table;
}

static int selected_lookup_callback(const kason_lookup_key *key,
                                    kason_data *data,
                                    int count,
                                    void *user_data)
{
    LookupResult *result = (LookupResult *)user_data;
    (void)count;

    std::size_t index;
    for (index = 0; index < benchmark_lookup_count; ++index) {
        if (key->value == benchmark_lookup_keys[index]) {
            break;
        }
    }
    uint64_t value;
    if (index >= benchmark_lookup_count ||
            kason_value_to_uint64(data->type, data->begin, data->end,
                                 &value) != KaSON_CONVERT_SUCCESS ||
            value != benchmark_lookup_values[index]) {
        return KaSON_CALLBACK_BREAK;
    }
    result->found |= 1U << index;
    result->sum += value;
    return KaSON_CALLBACK_CONTINUE;
}

static bool parse_kason_selected_lookup(const std::string &json)
{
    char *begin = const_cast<char *>(json.data());
    char *end = begin + json.size() - 1;
    const kason_lookup_table *table = prepared_lookup_table();
    LookupResult result = {0, 0};
    if (table == nullptr) {
        return false;
    }
    int parse_result = kason_parse_range_selected(
        begin, end, table, selected_lookup_callback, &result);

    if (parse_result != KaSON_PARSE_RESULT_SUCCESS ||
            result.found != benchmark_lookup_all_found) {
        return false;
    }
    bench_sink += result.sum;
    return true;
}

static const kason_schema_field benchmark_record_fields[] = {
    KaSON_FIELD_STRING(BenchmarkRecord, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_U32(BenchmarkRecord, id, "id", KaSON_REQUIRED),
    KaSON_FIELD_INT(BenchmarkRecord, offset, "offset", KaSON_REQUIRED),
    KaSON_FIELD_DOUBLE(BenchmarkRecord, ratio, "ratio", KaSON_REQUIRED)
};

KaSON_SCHEMA_DEFINE(benchmark_record_schema, BenchmarkRecord,
                   benchmark_record_fields, 8);

static bool parse_kason_struct(const std::string &json, unsigned flags)
{
    static bool initialized;
    BenchmarkRecord result = {};
    char *begin = const_cast<char *>(json.data());
    char *end = begin + json.size() - 1;

    if (!initialized) {
        if (kason_schema_init(&benchmark_record_schema) != KaSON_SCHEMA_SUCCESS) {
            return false;
        }
        initialized = true;
    }
    if (kason_unpack_range_flags(begin, end, &benchmark_record_schema,
                                &result, flags, nullptr) != KaSON_SCHEMA_SUCCESS ||
            !benchmark_record_valid(result)) {
        return false;
    }
    bench_sink += benchmark_record_checksum(result);
    return true;
}

static kason_schema_field benchmark_large_fields[benchmark_large_field_count];
static char benchmark_large_keys[benchmark_large_field_count][16];
static kason_lookup_key benchmark_large_lookup_slots[128];
static std::uint16_t benchmark_large_field_slots[128];
static kason_schema benchmark_large_schema;

static bool initialize_benchmark_large_schema()
{
    static bool initialized;

    if (initialized) {
        return true;
    }
    std::memset(&benchmark_large_schema, 0, sizeof(benchmark_large_schema));
    for (std::size_t i = 0; i < benchmark_large_field_count; ++i) {
        int length = std::snprintf(benchmark_large_keys[i],
                                   sizeof(benchmark_large_keys[i]),
                                   "field%zu", i);
        if (length <= 0 ||
                (std::size_t)length >= sizeof(benchmark_large_keys[i])) {
            return false;
        }
        kason_schema_field &field = benchmark_large_fields[i];
        std::memset(&field, 0, sizeof(field));
        field.json_key = benchmark_large_keys[i];
        field.key_length = length;
        field.offset = offsetof(BenchmarkLargeRecord, values) +
            i * sizeof(std::uint32_t);
        field.size = sizeof(std::uint32_t);
        field.type = KaSON_SCHEMA_TYPE_U32;
        field.policy.flags = KaSON_SCHEMA_POLICY_REQUIRED;
        field.count_offset = KaSON_SCHEMA_NO_OFFSET;
        field.element_size = sizeof(std::uint32_t);
        field.capacity = 1;
    }
    benchmark_large_schema.fields = benchmark_large_fields;
    benchmark_large_schema.field_count = (int)benchmark_large_field_count;
    benchmark_large_schema.object_size = sizeof(BenchmarkLargeRecord);
    benchmark_large_schema.lookup_slots = benchmark_large_lookup_slots;
    benchmark_large_schema.slot_field_indices = benchmark_large_field_slots;
    benchmark_large_schema.lookup_capacity = 128;
    if (kason_schema_init(&benchmark_large_schema) != KaSON_SCHEMA_SUCCESS) {
        return false;
    }
    initialized = true;
    return true;
}

static bool parse_kason_struct64(const std::string &json, unsigned flags)
{
    BenchmarkLargeRecord result = {};
    char *begin = const_cast<char *>(json.data());
    char *end = begin + json.size() - 1;

    if (!initialize_benchmark_large_schema() ||
            kason_unpack_range_flags(begin, end, &benchmark_large_schema,
                                    &result, flags, nullptr) !=
                KaSON_SCHEMA_SUCCESS ||
            !benchmark_large_record_valid(result)) {
        return false;
    }
    bench_sink += benchmark_large_record_checksum(result);
    return true;
}

struct ScanLookupResult {
    unsigned int found;
    unsigned long long sum;
};

static int scan_lookup_callback(kason_key *key, kason_data *data, int count,
                                void *user_data)
{
    ScanLookupResult *result = (ScanLookupResult *)user_data;
    (void)count;

    if (key == NULL || data->type != KaSON_TYPE_NUMBER) {
        return KaSON_CALLBACK_CONTINUE;
    }
    std::size_t key_length = (std::size_t)(key->end - key->begin + 1);
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        if (key_length == benchmark_lookup_lengths[i] &&
                std::memcmp(key->begin, benchmark_lookup_keys[i], key_length) == 0) {
            uint64_t value;
            if (kason_value_to_uint64(data->type, data->begin, data->end,
                                     &value) != KaSON_CONVERT_SUCCESS ||
                    value != benchmark_lookup_values[i]) {
                return KaSON_CALLBACK_BREAK;
            }
            result->found |= 1U << i;
            result->sum += value;
            break;
        }
    }
    return KaSON_CALLBACK_CONTINUE;
}

static bool parse_kason_lookup(const std::string &json)
{
    char *begin = const_cast<char *>(json.data());
    char *end = begin + json.size() - 1;
    ScanLookupResult result = {0, 0};
    int parse_result = kason_parse_range(begin, end, scan_lookup_callback, &result);

    if (parse_result != KaSON_PARSE_RESULT_SUCCESS ||
            result.found != benchmark_lookup_all_found) {
        return false;
    }
    bench_sink += result.sum;
    return true;
}

struct StreamNode {
    kason_stream stream;
    std::vector<char> scratch;
    StreamNode *child;
    BenchAccumulator *accumulator;
};

static int stream_recursive_callback(kason_key *key, kason_stream_data *data, void *user_data);

static StreamNode *new_stream_node(BenchAccumulator *accumulator)
{
    StreamNode *node = new StreamNode;
    node->scratch.resize(4096);
    node->child = NULL;
    node->accumulator = accumulator;
    if (kason_stream_init(&node->stream,
                         node->scratch.data(),
                         (int)node->scratch.size(),
                         stream_recursive_callback,
                         node) != KaSON_PARSE_RESULT_SUCCESS) {
        delete node;
        return NULL;
    }
    return node;
}

static void delete_stream_node(StreamNode *node)
{
    if (node == NULL) {
        return;
    }
    delete_stream_node(node->child);
    delete node;
}

static int stream_feed_child(StreamNode *node, kason_stream_data *data)
{
    int length = data->end >= data->begin
        ? (int)(data->end - data->begin + 1)
        : 0;
    int result;

    if (node->child == NULL) {
        node->child = new_stream_node(node->accumulator);
        if (node->child == NULL) {
            return KaSON_CALLBACK_BREAK;
        }
    }
    result = kason_stream_feed(&node->child->stream, data->begin, length);
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == KaSON_PARSE_RESULT_ERROR) {
        return KaSON_CALLBACK_BREAK;
    }
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        result = kason_stream_finish(&node->child->stream);
        if (result != KaSON_PARSE_RESULT_SUCCESS) {
            return KaSON_CALLBACK_BREAK;
        }
        delete_stream_node(node->child);
        node->child = NULL;
    }
    return KaSON_CALLBACK_CONTINUE;
}

static int stream_recursive_callback(kason_key *key, kason_stream_data *data, void *user_data)
{
    StreamNode *node = (StreamNode *)user_data;

    node->accumulator->value +=
        (unsigned long long)data->type + (unsigned long long)data->event;
    if (key != NULL && key->end >= key->begin) {
        node->accumulator->value +=
            (unsigned long long)(key->end - key->begin + 1);
    }
    if (data->end >= data->begin) {
        node->accumulator->value +=
            (unsigned long long)(data->end - data->begin + 1);
    }
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN ||
            data->event == KaSON_STREAM_EVENT_CONTAINER_PART ||
            data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        return stream_feed_child(node, data);
    }
    return KaSON_CALLBACK_CONTINUE;
}

static bool parse_kason_stream(const std::string &json)
{
    StreamNode root;
    int result;
    size_t offset = 0;
    const size_t chunk_size = 4096;
    BenchAccumulator accumulator = {0};

    root.scratch.resize(4096);
    root.child = NULL;
    root.accumulator = &accumulator;
    result = kason_stream_init(&root.stream,
                              root.scratch.data(),
                              (int)root.scratch.size(),
                              stream_recursive_callback,
                              &root);
    if (result != KaSON_PARSE_RESULT_SUCCESS) {
        return false;
    }
    while (offset < json.size()) {
        size_t remaining = json.size() - offset;
        size_t length = remaining < chunk_size ? remaining : chunk_size;
        result = kason_stream_feed(&root.stream,
                                  const_cast<char *>(json.data() + offset),
                                  (int)length);
        if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == KaSON_PARSE_RESULT_ERROR) {
            delete_stream_node(root.child);
            return false;
        }
        offset += length;
    }
    result = kason_stream_finish(&root.stream);
    delete_stream_node(root.child);
    bench_sink += accumulator.value;
    return result == KaSON_PARSE_RESULT_SUCCESS;
}

#if defined(KaSON_BENCH_CJSON)
static bool parse_cjson(const std::string &json)
{
    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());

    if (root == NULL) {
        return false;
    }
    bench_sink += (unsigned long long)root->type;
    bench_sink += (unsigned long long)cJSON_GetArraySize(root);
    cJSON_Delete(root);
    return true;
}

static bool parse_cjson_lookup(const std::string &json)
{
    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
    unsigned long long sum = 0;

    if (root == NULL) {
        return false;
    }
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(root, benchmark_lookup_keys[i]);
        if (!cJSON_IsNumber(value) ||
                (unsigned long long)value->valuedouble != benchmark_lookup_values[i]) {
            cJSON_Delete(root);
            return false;
        }
        sum += (unsigned long long)value->valuedouble;
    }
    bench_sink += sum;
    cJSON_Delete(root);
    return true;
}

static bool parse_cjson_struct(const std::string &json)
{
    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
    BenchmarkRecord record = {};
    bool valid;

    if (root == NULL) {
        return false;
    }
    cJSON *name = cJSON_GetObjectItemCaseSensitive(
        root, benchmark_record_name_key);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, benchmark_record_id_key);
    cJSON *offset = cJSON_GetObjectItemCaseSensitive(
        root, benchmark_record_offset_key);
    cJSON *ratio = cJSON_GetObjectItemCaseSensitive(
        root, benchmark_record_ratio_key);
    valid = cJSON_IsObject(root) && cJSON_IsString(name) &&
        cJSON_IsNumber(id) && cJSON_IsNumber(offset) &&
        cJSON_IsNumber(ratio) &&
        benchmark_record_set_name(record, name->valuestring,
                                  std::strlen(name->valuestring));
    if (valid) {
        valid = id->valuedouble >= 0.0 && id->valuedouble <= UINT32_MAX &&
            offset->valuedouble >= INT_MIN && offset->valuedouble <= INT_MAX;
        if (valid) {
            record.id = (std::uint32_t)id->valuedouble;
            record.offset = (int)offset->valuedouble;
            record.ratio = ratio->valuedouble;
            valid = (double)record.id == id->valuedouble &&
                (double)record.offset == offset->valuedouble;
        }
    }
    valid = valid && benchmark_record_valid(record);
    if (valid) {
        bench_sink += benchmark_record_checksum(record);
    }
    cJSON_Delete(root);
    return valid;
}
#endif

#if defined(KaSON_BENCH_SIMDJSON)
static bool parse_simdjson(const simdjson::padded_string &json)
{
    static thread_local simdjson::dom::parser parser;
    simdjson::dom::element doc;
    simdjson::dom::element_type type;
    simdjson::error_code error = parser.parse(json).get(doc);

    if (error) {
        return false;
    }
    type = doc.type();
    bench_sink += (unsigned long long)type;
    return true;
}

static bool parse_simdjson_lookup(const simdjson::padded_string &json)
{
    static thread_local simdjson::dom::parser parser;
    simdjson::dom::element doc;
    simdjson::dom::object object;
    unsigned long long sum = 0;

    if (parser.parse(json).get(doc) || doc.get_object().get(object)) {
        return false;
    }
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        simdjson::dom::element value;
        int64_t int_value;
        if (object.at_key(benchmark_lookup_keys[i]).get(value) ||
                value.get_int64().get(int_value) ||
                (unsigned long long)int_value != benchmark_lookup_values[i]) {
            return false;
        }
        sum += (unsigned long long)int_value;
    }
    bench_sink += sum;
    return true;
}

static bool parse_simdjson_struct(const simdjson::padded_string &json)
{
    static thread_local simdjson::dom::parser parser;
    simdjson::dom::element doc;
    simdjson::dom::object object;
    std::string_view name;
    std::uint64_t id;
    std::int64_t offset;
    double ratio;
    BenchmarkRecord record = {};

    if (parser.parse(json).get(doc) || doc.get_object().get(object) ||
            object[benchmark_record_name_key].get_string().get(name) ||
            object[benchmark_record_id_key].get_uint64().get(id) ||
            object[benchmark_record_offset_key].get_int64().get(offset) ||
            object[benchmark_record_ratio_key].get_double().get(ratio) ||
            id > UINT32_MAX || offset < INT_MIN || offset > INT_MAX ||
            !benchmark_record_set_name(record, name.data(), name.size())) {
        return false;
    }
    record.id = (std::uint32_t)id;
    record.offset = (int)offset;
    record.ratio = ratio;
    if (!benchmark_record_valid(record)) {
        return false;
    }
    bench_sink += benchmark_record_checksum(record);
    return true;
}
#endif

#if defined(KaSON_BENCH_JSON_GLIB)
static bool parse_json_glib(const std::string &json)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gboolean ok = json_parser_load_from_data(parser,
                                             json.data(),
                                             (gssize)json.size(),
                                             &error);

    if (!ok) {
        if (error != NULL) {
            g_error_free(error);
        }
        g_object_unref(parser);
        return false;
    }
    JsonNode *root = json_parser_get_root(parser);
    bench_sink += (unsigned long long)json_node_get_node_type(root);
    g_object_unref(parser);
    return true;
}

static bool parse_json_glib_lookup(const std::string &json)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gboolean ok = json_parser_load_from_data(parser,
                                             json.data(),
                                             (gssize)json.size(),
                                             &error);
    unsigned long long sum = 0;

    if (!ok) {
        if (error != NULL) {
            g_error_free(error);
        }
        g_object_unref(parser);
        return false;
    }
    JsonObject *object = json_node_get_object(json_parser_get_root(parser));
    for (std::size_t i = 0; i < benchmark_lookup_count; ++i) {
        if (!json_object_has_member(object, benchmark_lookup_keys[i])) {
            g_object_unref(parser);
            return false;
        }
        gint64 value = json_object_get_int_member(object, benchmark_lookup_keys[i]);
        if ((unsigned long long)value != benchmark_lookup_values[i]) {
            g_object_unref(parser);
            return false;
        }
        sum += (unsigned long long)value;
    }
    bench_sink += sum;
    g_object_unref(parser);
    return true;
}

static bool parse_json_glib_struct(const std::string &json)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gboolean ok = json_parser_load_from_data(parser,
                                             json.data(),
                                             (gssize)json.size(),
                                             &error);
    BenchmarkRecord record = {};
    bool valid = false;

    if (ok) {
        JsonNode *root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *object = json_node_get_object(root);
            JsonNode *name_node = json_object_get_member(
                object, benchmark_record_name_key);
            JsonNode *id_node = json_object_get_member(
                object, benchmark_record_id_key);
            JsonNode *offset_node = json_object_get_member(
                object, benchmark_record_offset_key);
            JsonNode *ratio_node = json_object_get_member(
                object, benchmark_record_ratio_key);
            valid = name_node != NULL && id_node != NULL &&
                offset_node != NULL && ratio_node != NULL &&
                json_node_get_value_type(name_node) == G_TYPE_STRING &&
                json_node_get_value_type(id_node) == G_TYPE_INT64 &&
                json_node_get_value_type(offset_node) == G_TYPE_INT64 &&
                json_node_get_value_type(ratio_node) == G_TYPE_DOUBLE;
            if (valid) {
                const gchar *name = json_node_get_string(name_node);
                gint64 id = json_node_get_int(id_node);
                gint64 offset = json_node_get_int(offset_node);
                valid = id >= 0 && (guint64)id <= UINT32_MAX &&
                    offset >= INT_MIN && offset <= INT_MAX &&
                    benchmark_record_set_name(record, name,
                                              std::strlen(name));
                if (valid) {
                    record.id = (std::uint32_t)id;
                    record.offset = (int)offset;
                    record.ratio = json_node_get_double(ratio_node);
                }
            }
        }
    }
    if (error != NULL) {
        g_error_free(error);
    }
    valid = valid && benchmark_record_valid(record);
    if (valid) {
        bench_sink += benchmark_record_checksum(record);
    }
    g_object_unref(parser);
    return valid;
}
#endif

#if defined(KaSON_BENCH_SIMDJSON)
using benchmark_padded_string = simdjson::padded_string;
#else
using benchmark_padded_string = std::string;
#endif

static void *hold_parse(const char *parser_name,
                        const std::string &json,
                        const benchmark_padded_string &padded)
{
    (void)padded;
    if (strcmp(parser_name, "kason-range") == 0) {
        return parse_kason_range(json) ? (void *)1 : NULL;
    }
    if (strcmp(parser_name, "kason-selected") == 0) {
        return parse_kason_range(json) ? (void *)1 : NULL;
    }
    if (strcmp(parser_name, "kason-schema") == 0) {
        return parse_kason_range(json) ? (void *)1 : NULL;
    }
    if (strcmp(parser_name, "kason-schema-fast") == 0) {
        return parse_kason_range(json) ? (void *)1 : NULL;
    }
    if (strcmp(parser_name, "kason-stream") == 0) {
        return parse_kason_stream(json) ? (void *)1 : NULL;
    }
#if defined(KaSON_BENCH_CJSON)
    if (strcmp(parser_name, "cjson") == 0) {
        cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
        if (root != NULL) {
            bench_sink += (unsigned long long)cJSON_GetArraySize(root);
        }
        return root;
    }
#endif
#if defined(KaSON_BENCH_SIMDJSON)
    if (strcmp(parser_name, "simdjson") == 0) {
        simdjson::dom::parser *parser = new simdjson::dom::parser;
        simdjson::dom::element doc;
        simdjson::error_code error = parser->parse(padded).get(doc);
        if (error) {
            delete parser;
            return NULL;
        }
        bench_sink += 1;
        return parser;
    }
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
    if (strcmp(parser_name, "json-glib") == 0) {
        JsonParser *parser = json_parser_new();
        GError *error = NULL;
        gboolean ok = json_parser_load_from_data(parser,
                                                 json.data(),
                                                 (gssize)json.size(),
                                                 &error);
        if (!ok) {
            if (error != NULL) {
                g_error_free(error);
            }
            g_object_unref(parser);
            return NULL;
        }
        bench_sink += 1;
        return parser;
    }
#endif
    return NULL;
}

static void release_hold(const char *parser_name, void *hold)
{
    (void)parser_name;
    if (hold == NULL || hold == (void *)1) {
        return;
    }
#if defined(KaSON_BENCH_CJSON)
    if (strcmp(parser_name, "cjson") == 0) {
        cJSON_Delete((cJSON *)hold);
        return;
    }
#endif
#if defined(KaSON_BENCH_SIMDJSON)
    if (strcmp(parser_name, "simdjson") == 0) {
        delete (simdjson::dom::parser *)hold;
        return;
    }
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
    if (strcmp(parser_name, "json-glib") == 0) {
        g_object_unref((JsonParser *)hold);
        return;
    }
#endif
}

static bool parse_for_speed(const char *parser_name,
                            const std::string &json,
                            const benchmark_padded_string &padded)
{
    (void)padded;
    if (strcmp(parser_name, "kason-range") == 0) {
        return parse_kason_range(json);
    }
    if (strcmp(parser_name, "kason-stream") == 0) {
        return parse_kason_stream(json);
    }
#if defined(KaSON_BENCH_CJSON)
    if (strcmp(parser_name, "cjson") == 0) {
        return parse_cjson(json);
    }
#endif
#if defined(KaSON_BENCH_SIMDJSON)
    if (strcmp(parser_name, "simdjson") == 0) {
        return parse_simdjson(padded);
    }
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
    if (strcmp(parser_name, "json-glib") == 0) {
        return parse_json_glib(json);
    }
#endif
    return false;
}

static bool parse_for_lookup(const char *parser_name,
                             const std::string &json,
                             const benchmark_padded_string &padded)
{
    (void)padded;
    if (strcmp(parser_name, "kason-range") == 0) {
        return parse_kason_lookup(json);
    }
    if (strcmp(parser_name, "kason-selected") == 0) {
        return parse_kason_selected_lookup(json);
    }
#if defined(KaSON_BENCH_CJSON)
    if (strcmp(parser_name, "cjson") == 0) {
        return parse_cjson_lookup(json);
    }
#endif
#if defined(KaSON_BENCH_SIMDJSON)
    if (strcmp(parser_name, "simdjson") == 0) {
        return parse_simdjson_lookup(padded);
    }
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
    if (strcmp(parser_name, "json-glib") == 0) {
        return parse_json_glib_lookup(json);
    }
#endif
    return false;
}

static bool parse_for_struct(const char *parser_name,
                             const std::string &json,
                             const benchmark_padded_string &padded,
                             bool struct64_case)
{
    (void)padded;
    if (strcmp(parser_name, "kason-schema") == 0) {
        return struct64_case
            ? parse_kason_struct64(json, 0)
            : parse_kason_struct(json, 0);
    }
    if (strcmp(parser_name, "kason-schema-fast") == 0) {
        return struct64_case
            ? parse_kason_struct64(json, KaSON_UNPACK_RELAXED)
            : parse_kason_struct(json, KaSON_UNPACK_RELAXED);
    }
#if defined(KaSON_BENCH_CJSON)
    if (strcmp(parser_name, "cjson") == 0) {
        return parse_cjson_struct(json);
    }
#endif
#if defined(KaSON_BENCH_SIMDJSON)
    if (strcmp(parser_name, "simdjson") == 0) {
        return parse_simdjson_struct(padded);
    }
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
    if (strcmp(parser_name, "json-glib") == 0) {
        return parse_json_glib_struct(json);
    }
#endif
    return false;
}

static void print_available_parsers()
{
    std::printf("kason-range kason-selected kason-schema kason-schema-fast kason-stream");
#if defined(KaSON_BENCH_CJSON)
    std::printf(" cjson");
#endif
#if defined(KaSON_BENCH_SIMDJSON)
    std::printf(" simdjson");
#endif
#if defined(KaSON_BENCH_JSON_GLIB)
    std::printf(" json-glib");
#endif
#if defined(KaSON_BENCH_HAVE_JSON_C)
    std::printf(" json-c");
#endif
#if defined(KaSON_BENCH_HAVE_JANSSON)
    std::printf(" jansson");
#endif
#if defined(KaSON_BENCH_HAVE_YYJSON)
    std::printf(" yyjson");
#endif
#if defined(KaSON_BENCH_HAVE_JSMN)
    std::printf(" jsmn");
#endif
#if defined(KaSON_BENCH_HAVE_PARSON)
    std::printf(" parson");
#endif
    std::printf("\n");
}

static void print_usage(const char *argv0)
{
    std::fprintf(stderr,
                 "usage: %s <kason-range|kason-selected|kason-schema|kason-schema-fast|kason-stream|cjson|simdjson|json-glib> <flat|nested|lookup|struct|struct64|struct64-sparse> <iterations>\n",
                 argv0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--list-parsers") == 0) {
        print_available_parsers();
        return 0;
    }
    if (argc != 4) {
        print_usage(argv[0]);
        return 2;
    }

    const char *parser_name = argv[1];
    const char *case_name = argv[2];
    int iterations = std::atoi(argv[3]);
    if (iterations <= 0) {
        print_usage(argv[0]);
        return 2;
    }

    std::string json;
    bool lookup_case = strcmp(case_name, "lookup") == 0;
    bool struct_case = strcmp(case_name, "struct") == 0;
    bool struct64_case = strcmp(case_name, "struct64") == 0;
    bool struct64_sparse_case = strcmp(case_name, "struct64-sparse") == 0;
    if (strcmp(case_name, "flat") == 0 || lookup_case) {
        json = make_flat_json();
    } else if (strcmp(case_name, "nested") == 0) {
        json = make_nested_json();
    } else if (struct_case) {
        json = make_struct_json();
    } else if (struct64_case) {
        json = make_struct64_json();
    } else if (struct64_sparse_case) {
        json = make_struct64_sparse_json();
    } else {
        print_usage(argv[0]);
        return 2;
    }
    benchmark_padded_string padded(json);

    unsigned long long rss_before = benchmark_rss_bytes();
    void *hold = hold_parse(parser_name, json, padded);
    if (hold == NULL) {
        std::fprintf(stderr, "parse failed for %s/%s\n", parser_name, case_name);
        return 1;
    }
    unsigned long long rss_after = benchmark_rss_bytes();
    release_hold(parser_name, hold);

    if (!(lookup_case
              ? parse_for_lookup(parser_name, json, padded)
              : (struct_case || struct64_case || struct64_sparse_case
                    ? parse_for_struct(parser_name, json, padded,
                                       struct64_case || struct64_sparse_case)
                    : parse_for_speed(parser_name, json, padded)))) {
        std::fprintf(stderr, "warmup failed for %s/%s\n", parser_name, case_name);
        return 1;
    }

    double start = now_seconds();
    for (int i = 0; i < iterations; ++i) {
        if (!(lookup_case
                  ? parse_for_lookup(parser_name, json, padded)
                  : (struct_case || struct64_case || struct64_sparse_case
                        ? parse_for_struct(parser_name, json, padded,
                                           struct64_case || struct64_sparse_case)
                        : parse_for_speed(parser_name, json, padded)))) {
            std::fprintf(stderr, "timed parse failed for %s/%s\n", parser_name, case_name);
            return 1;
        }
    }
    double elapsed = now_seconds() - start;
    double mib = ((double)json.size() * (double)iterations) / (1024.0 * 1024.0);
    double mib_per_second = elapsed > 0.0 ? mib / elapsed : 0.0;
    long long rss_delta = (long long)rss_after - (long long)rss_before;

    std::printf("parser,case,bytes,iterations,seconds,mib_per_second,rss_before,rss_after,rss_delta,sink\n");
    std::printf("%s,%s,%zu,%d,%.6f,%.2f,%llu,%llu,%lld,%llu\n",
                parser_name,
                case_name,
                json.size(),
                iterations,
                elapsed,
                mib_per_second,
                rss_before,
                rss_after,
                rss_delta,
                (unsigned long long)bench_sink);
    return 0;
}
