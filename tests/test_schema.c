#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kason_schema.h"

typedef struct s_test_endpoint {
    char host[16];
    uint32_t port;
} test_endpoint;

typedef struct s_test_config {
    char name[16];
    uint32_t id;
    int offset;
    int64_t sequence;
    uint64_t mask;
    double ratio;
    int active;
    uint16_t title16[16];
    uint32_t title32[16];
    test_endpoint network;
    test_endpoint backup;
    int values[3];
    size_t value_count;
    test_endpoint children[2];
    size_t child_count;
    char tags[2][8];
    size_t tag_count;
    uint16_t tags16[2][8];
    size_t tag16_count;
    uint32_t tags32[2][8];
    size_t tag32_count;
} test_config;

static const test_endpoint default_backup = {"localhost", 1883};
static const uint16_t default_title16[] = {'d', 0x03a9, 0};
static const uint32_t default_title32[] = {'d', 0x1f600, 0};

static const kason_schema_field endpoint_fields[] = {
    KaSON_FIELD_STRING(test_endpoint, host, "host", KaSON_REQUIRED),
    KaSON_FIELD_U32(test_endpoint, port, "port", KaSON_DEFAULT_U32(1883))
};

KaSON_SCHEMA_DEFINE(endpoint_schema, test_endpoint, endpoint_fields, 4);

static const kason_schema_field config_fields[] = {
    KaSON_FIELD_STRING(test_config, name, "name", KaSON_REQUIRED),
    KaSON_FIELD_U32(test_config, id, "id", KaSON_REQUIRED),
    KaSON_FIELD_INT(test_config, offset, "offset", KaSON_DEFAULT_INT(-7)),
    KaSON_FIELD_INT64(test_config, sequence, "sequence", KaSON_DEFAULT_INT64(-9)),
    KaSON_FIELD_U64(test_config, mask, "mask", KaSON_DEFAULT_U64(UINT64_C(99))),
    KaSON_FIELD_DOUBLE(test_config, ratio, "ratio", KaSON_DEFAULT_DOUBLE(1.0)),
    KaSON_FIELD_BOOL(test_config, active, "active", KaSON_DEFAULT_BOOL(1)),
    KaSON_FIELD_STRING_U16(test_config, title16, "title16",
                          KaSON_DEFAULT_STRING_U16(default_title16)),
    KaSON_FIELD_STRING_U32(test_config, title32, "title32",
                          KaSON_DEFAULT_STRING_U32(default_title32)),
    KaSON_FIELD_STRUCT(test_config, network, "network", &endpoint_schema,
                      KaSON_REQUIRED),
    KaSON_FIELD_STRUCT(test_config, backup, "backup", &endpoint_schema,
                      KaSON_DEFAULT_STRUCT(default_backup)),
    KaSON_FIELD_INT_ARRAY(test_config, values, value_count, "values",
                         KaSON_DEFAULT_EMPTY_ARRAY),
    KaSON_FIELD_STRUCT_ARRAY(test_config, children, child_count, "children",
                            &endpoint_schema, KaSON_DEFAULT_EMPTY_ARRAY),
    KaSON_FIELD_STRING_ARRAY(test_config, tags, tag_count, "tags",
                            KaSON_DEFAULT_EMPTY_ARRAY),
    KaSON_FIELD_STRING_U16_ARRAY(test_config, tags16, tag16_count, "tags16",
                                KaSON_DEFAULT_EMPTY_ARRAY),
    KaSON_FIELD_STRING_U32_ARRAY(test_config, tags32, tag32_count, "tags32",
                                KaSON_DEFAULT_EMPTY_ARRAY)
};

KaSON_SCHEMA_DEFINE(config_schema, test_config, config_fields, 32);

typedef struct {
    int first;
    int second;
} invalid_config;

static const kason_schema_field duplicate_fields[] = {
    KaSON_FIELD_INT(invalid_config, first, "same", KaSON_REQUIRED),
    KaSON_FIELD_INT(invalid_config, second, "same", KaSON_REQUIRED)
};

KaSON_SCHEMA_DEFINE(duplicate_schema, invalid_config, duplicate_fields, 4);

typedef struct {
    int value;
} relaxed_config;

static const kason_schema_field relaxed_fields[] = {
    KaSON_FIELD_INT(relaxed_config, value, "value", KaSON_REQUIRED)
};

KaSON_SCHEMA_DEFINE(relaxed_schema, relaxed_config, relaxed_fields, 2);

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                      \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
            return 0;                                                           \
        }                                                                       \
    } while (0)

static int initialize_schemas(void)
{
    CHECK(kason_schema_init(&endpoint_schema) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_schema_init(&config_schema) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_schema_init(&relaxed_schema) == KaSON_SCHEMA_SUCCESS);
    return 1;
}

static int test_invalid_schema(void)
{
    kason_schema copy = config_schema;

    CHECK(kason_schema_init(NULL) == KaSON_SCHEMA_ERROR_INVALID_SCHEMA);
    CHECK(kason_schema_init(&duplicate_schema) == KaSON_SCHEMA_ERROR_INVALID_SCHEMA);
    copy.lookup_capacity = 1;
    CHECK(kason_schema_init(&copy) == KaSON_SCHEMA_ERROR_INVALID_SCHEMA);
    return 1;
}

static int test_unpack_nested_defaults_arrays(void)
{
    char json[] =
        "{\"ignored\":0,\"name\":\"A\\nB\",\"id\":42,"
        "\"sequence\":-9223372036854775808,"
        "\"mask\":18446744073709551615,"
        "\"ratio\":2.5,\"active\":false,"
        "\"title16\":\"A\\u03a9\\ud83d\\ude00\","
        "\"title32\":\"A\\u03a9\\ud83d\\ude00\","
        "\"network\":{\"host\":\"broker\"},"
        "\"values\":[1,-2,3],"
        "\"children\":[{\"host\":\"one\",\"port\":1},{\"host\":\"two\"}],"
        "\"tags\":[\"red\",\"blue\"],"
        "\"tags16\":[\"x\",\"\\u03a9\"],"
        "\"tags32\":[\"x\",\"\\ud83d\\ude00\"]}";
    test_config value;
    kason_schema_error error;

    memset(&value, 0xa5, sizeof(value));
    CHECK(kason_unpack(json, &config_schema, &value, &error) == KaSON_SCHEMA_SUCCESS);
    CHECK(error.code == KaSON_SCHEMA_SUCCESS);
    CHECK(strcmp(value.name, "A\nB") == 0);
    CHECK(value.id == 42);
    CHECK(value.offset == -7);
    CHECK(value.sequence == INT64_MIN);
    CHECK(value.mask == UINT64_MAX);
    CHECK(value.ratio == 2.5);
    CHECK(value.active == 0);
    CHECK(value.title16[0] == 'A' && value.title16[1] == 0x03a9);
    CHECK(value.title16[2] == 0xd83d && value.title16[3] == 0xde00 &&
          value.title16[4] == 0);
    CHECK(value.title32[0] == 'A' && value.title32[1] == 0x03a9 &&
          value.title32[2] == 0x1f600 && value.title32[3] == 0);
    CHECK(strcmp(value.network.host, "broker") == 0);
    CHECK(value.network.port == 1883);
    CHECK(strcmp(value.backup.host, "localhost") == 0);
    CHECK(value.backup.port == 1883);
    CHECK(value.value_count == 3);
    CHECK(value.values[0] == 1 && value.values[1] == -2 && value.values[2] == 3);
    CHECK(value.child_count == 2);
    CHECK(strcmp(value.children[0].host, "one") == 0);
    CHECK(value.children[0].port == 1);
    CHECK(strcmp(value.children[1].host, "two") == 0);
    CHECK(value.children[1].port == 1883);
    CHECK(value.tag_count == 2);
    CHECK(strcmp(value.tags[0], "red") == 0);
    CHECK(strcmp(value.tags[1], "blue") == 0);
    CHECK(value.tag16_count == 2);
    CHECK(value.tags16[0][0] == 'x' && value.tags16[0][1] == 0);
    CHECK(value.tags16[1][0] == 0x03a9 && value.tags16[1][1] == 0);
    CHECK(value.tag32_count == 2);
    CHECK(value.tags32[0][0] == 'x' && value.tags32[0][1] == 0);
    CHECK(value.tags32[1][0] == 0x1f600 && value.tags32[1][1] == 0);
    return 1;
}

static int test_unicode_string_defaults(void)
{
    char json[] = "{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"}}";
    test_config value;

    memset(&value, 0, sizeof(value));
    CHECK(kason_unpack(json, &config_schema, &value, NULL) == KaSON_SCHEMA_SUCCESS);
    CHECK(value.title16[0] == 'd' && value.title16[1] == 0x03a9 &&
          value.title16[2] == 0);
    CHECK(value.title32[0] == 'd' && value.title32[1] == 0x1f600 &&
          value.title32[2] == 0);
    CHECK(value.sequence == -9);
    CHECK(value.mask == 99);
    return 1;
}

static int test_unpack_errors(void)
{
    struct error_case {
        const char *json;
        int expected;
    } cases[] = {
        {"{\"name\":\"x\",\"network\":{\"host\":\"h\"}}",
         KaSON_SCHEMA_ERROR_MISSING_FIELD},
        {"{\"name\":\"x\",\"id\":1,\"id\":2,\"network\":{\"host\":\"h\"}}",
         KaSON_SCHEMA_ERROR_DUPLICATE_FIELD},
        {"{\"name\":\"x\",\"id\":-1,\"network\":{\"host\":\"h\"}}",
         KaSON_SCHEMA_ERROR_RANGE},
        {"{\"name\":\"x\",\"id\":18446744073709551616.5,"
         "\"network\":{\"host\":\"h\"}}", KaSON_SCHEMA_ERROR_RANGE},
        {"{\"name\":\"x\",\"id\":1,"
         "\"sequence\":18446744073709551616e-1,"
         "\"network\":{\"host\":\"h\"}}", KaSON_SCHEMA_ERROR_RANGE},
        {"{\"name\":4,\"id\":1,\"network\":{\"host\":\"h\"}}",
         KaSON_SCHEMA_ERROR_TYPE},
        {"42", KaSON_SCHEMA_ERROR_TYPE},
        {"{\"name\":\"x\\u0000y\",\"id\":1,\"network\":{\"host\":\"h\"}}",
         KaSON_SCHEMA_ERROR_TYPE},
        {"{\"name\":\"0123456789abcdef\",\"id\":1,\"network\":{\"host\":\"h\"}}",
         KaSON_SCHEMA_ERROR_STRING_CAPACITY},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"title16\":\"0123456789abcdef\"}", KaSON_SCHEMA_ERROR_STRING_CAPACITY},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"title32\":\"0123456789abcdef\"}", KaSON_SCHEMA_ERROR_STRING_CAPACITY},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"title16\":\"x\\u0000y\"}", KaSON_SCHEMA_ERROR_TYPE},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"title32\":\"x\\u0000y\"}", KaSON_SCHEMA_ERROR_TYPE},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"port\":1}}",
         KaSON_SCHEMA_ERROR_MISSING_FIELD},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"backup\":{\"port\":1}}", KaSON_SCHEMA_ERROR_MISSING_FIELD},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"values\":[1,2,3,4]}", KaSON_SCHEMA_ERROR_ARRAY_CAPACITY},
        {"{\"name\":\"x\",\"id\":1,\"network\":{\"host\":\"h\"},"
         "\"values\":null}", KaSON_SCHEMA_ERROR_TYPE}
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char input[256];
        test_config value = {0};
        kason_schema_error error;
        CHECK(strlen(cases[i].json) < sizeof(input));
        strcpy(input, cases[i].json);
        CHECK(kason_unpack(input, &config_schema, &value, &error) == cases[i].expected);
        CHECK(error.code == cases[i].expected);
    }
    return 1;
}

static int test_relaxed_unpack(void)
{
    char invalid_tail[] = "{\"value\":7,\"broken\":}";
    char invalid_tail_range[] = "{\"value\":8,\"broken\":}";
    char duplicate[] = "{\"value\":9,\"value\":10}";
    char missing[] = "{\"other\":1}";
    relaxed_config value = {0};
    kason_schema_error error;

    CHECK(kason_unpack(invalid_tail, &relaxed_schema, &value, &error) ==
          KaSON_SCHEMA_ERROR);
    CHECK(kason_unpack_flags(invalid_tail, &relaxed_schema, &value,
                            KaSON_UNPACK_RELAXED, &error) == KaSON_SCHEMA_SUCCESS);
    CHECK(value.value == 7);

    CHECK(kason_unpack_range_flags(invalid_tail_range,
                                  invalid_tail_range + strlen(invalid_tail_range) - 1,
                                  &relaxed_schema, &value,
                                  KaSON_UNPACK_RELAXED, &error) ==
          KaSON_SCHEMA_SUCCESS);
    CHECK(value.value == 8);

    CHECK(kason_unpack(duplicate, &relaxed_schema, &value, &error) ==
          KaSON_SCHEMA_ERROR_DUPLICATE_FIELD);
    CHECK(kason_unpack_flags(duplicate, &relaxed_schema, &value,
                            KaSON_UNPACK_RELAXED, &error) == KaSON_SCHEMA_SUCCESS);
    CHECK(value.value == 9);

    CHECK(kason_unpack_flags(missing, &relaxed_schema, &value,
                            KaSON_UNPACK_RELAXED, &error) ==
          KaSON_SCHEMA_ERROR_MISSING_FIELD);
    CHECK(kason_unpack_flags(invalid_tail, &relaxed_schema, &value,
                            2U, &error) == KaSON_SCHEMA_ERROR_INVALID_SCHEMA);
    return 1;
}

typedef struct s_callback_output {
    char data[1024];
    size_t length;
    int fail;
} callback_output;

static int collect_output(const char *data, size_t length, void *user_data)
{
    callback_output *output = (callback_output *)user_data;
    if (output->fail || length > sizeof(output->data) - output->length - 1) {
        return 1;
    }
    memcpy(output->data + output->length, data, length);
    output->length += length;
    output->data[output->length] = '\0';
    return 0;
}

static void make_pack_value(test_config *value)
{
    memset(value, 0, sizeof(*value));
    strcpy(value->name, "A\nB");
    value->id = 42;
    value->offset = -7;
    value->sequence = -9;
    value->mask = 99;
    value->ratio = 2.5;
    value->active = 1;
    value->title16[0] = 'U';
    value->title16[1] = 0x03a9;
    value->title16[2] = 0;
    value->title32[0] = 'U';
    value->title32[1] = 0x1f600;
    value->title32[2] = 0;
    strcpy(value->network.host, "broker");
    value->network.port = 1883;
    value->backup = default_backup;
    value->values[0] = 1;
    value->values[1] = -2;
    value->value_count = 2;
    strcpy(value->children[0].host, "child");
    value->children[0].port = 9;
    value->child_count = 1;
    strcpy(value->tags[0], "red");
    value->tag_count = 1;
    value->tags16[0][0] = 0x03a9;
    value->tags16[0][1] = 0;
    value->tag16_count = 1;
    value->tags32[0][0] = 0x1f600;
    value->tags32[0][1] = 0;
    value->tag32_count = 1;
}

static int test_pack_buffer_counter_callback_roundtrip(void)
{
    test_config value;
    test_config decoded;
    char json[1024];
    char scratch[17];
    kason_writer writer;
    kason_writer counter;
    kason_writer callback_writer;
    kason_schema_error error;
    callback_output output = {{0}, 0, 0};
    size_t length;

    make_pack_value(&value);
    CHECK(kason_writer_init_buffer(&writer, json, sizeof(json)) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value,
                    KaSON_PACK_OMIT_DEFAULTS, &error) == KaSON_SCHEMA_SUCCESS);
    length = kason_writer_length(&writer);
    CHECK(length == strlen(json));
    CHECK(strstr(json, "\"name\":\"A\\nB\"") != NULL);
    CHECK(strstr(json, "\"offset\"") == NULL);
    CHECK(strstr(json, "\"backup\"") == NULL);
    CHECK(strstr(json, "\"values\":[1,-2]") != NULL);
    CHECK(strstr(json, "\"title16\":\"U") != NULL);
    CHECK(strstr(json, "\"title32\":\"U") != NULL);

    memset(&decoded, 0, sizeof(decoded));
    CHECK(kason_unpack(json, &config_schema, &decoded, &error) == KaSON_SCHEMA_SUCCESS);
    CHECK(strcmp(decoded.name, value.name) == 0);
    CHECK(decoded.id == value.id);
    CHECK(decoded.ratio == value.ratio);
    CHECK(decoded.value_count == value.value_count);
    CHECK(decoded.child_count == value.child_count);
    CHECK(memcmp(decoded.title16, value.title16, sizeof(value.title16)) == 0);
    CHECK(memcmp(decoded.title32, value.title32, sizeof(value.title32)) == 0);
    CHECK(decoded.tag16_count == 1 && decoded.tags16[0][0] == 0x03a9);
    CHECK(decoded.tag32_count == 1 && decoded.tags32[0][0] == 0x1f600);

    CHECK(kason_writer_init_counter(&counter) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&counter, &config_schema, &value,
                    KaSON_PACK_OMIT_DEFAULTS, NULL) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_writer_length(&counter) == length);

    CHECK(kason_writer_init_callback(&callback_writer, scratch, sizeof(scratch),
                                    collect_output, &output) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&callback_writer, &config_schema, &value,
                    KaSON_PACK_OMIT_DEFAULTS, NULL) == KaSON_SCHEMA_SUCCESS);
    CHECK(output.length == length);
    CHECK(strcmp(output.data, json) == 0);
    return 1;
}

static int test_writer_errors(void)
{
    test_config value;
    char small[16];
    char invalid_utf8[] = {(char)0xc0, (char)0x80, 0};
    kason_writer writer;
    kason_schema_error error;
    callback_output output = {{0}, 0, 1};

    make_pack_value(&value);
    CHECK(kason_writer_init_buffer(&writer, small, sizeof(small)) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_WRITER_CAPACITY);

    CHECK(kason_writer_init_callback(&writer, NULL, 0, collect_output, &output) ==
          KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_WRITER_CALLBACK);

    strcpy(value.name, invalid_utf8);
    CHECK(kason_writer_init_counter(&writer) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_TYPE);

    make_pack_value(&value);
    value.ratio = HUGE_VAL;
    CHECK(kason_writer_init_counter(&writer) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_TYPE);

    make_pack_value(&value);
    value.title16[0] = 0xd800;
    value.title16[1] = 0;
    CHECK(kason_writer_init_counter(&writer) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_TYPE);

    make_pack_value(&value);
    value.title32[0] = 0xd800;
    value.title32[1] = 0;
    CHECK(kason_writer_init_counter(&writer) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_TYPE);

    make_pack_value(&value);
    value.value_count = 4;
    CHECK(kason_writer_init_counter(&writer) == KaSON_SCHEMA_SUCCESS);
    CHECK(kason_pack(&writer, &config_schema, &value, 0, &error) ==
          KaSON_SCHEMA_ERROR_ARRAY_CAPACITY);
    return 1;
}

static int test_double_roundtrip(void)
{
    static const double values[] = {
        0.0, -0.0, 1.0 / 3.0, DBL_MIN, DBL_MAX, 1.0e-200, -1.0e200
    };
    size_t i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        test_config input;
        test_config output;
        char json[1024];
        kason_writer writer;

        make_pack_value(&input);
        input.ratio = values[i];
        CHECK(kason_writer_init_buffer(&writer, json, sizeof(json)) ==
              KaSON_SCHEMA_SUCCESS);
        CHECK(kason_pack(&writer, &config_schema, &input, 0, NULL) ==
              KaSON_SCHEMA_SUCCESS);
        CHECK(kason_unpack(json, &config_schema, &output, NULL) ==
              KaSON_SCHEMA_SUCCESS);
        CHECK(memcmp(&input.ratio, &output.ratio, sizeof(input.ratio)) == 0);
    }
    return 1;
}

static void run_test(const char *name, int (*test_function)(void))
{
    printf("%-40s", name);
    if (test_function()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
}

int main(void)
{
    run_test("schema initialization", initialize_schemas);
    if (failures == 0) {
        run_test("invalid schema", test_invalid_schema);
        run_test("unpack nested/defaults/arrays", test_unpack_nested_defaults_arrays);
        run_test("UTF-16/UTF-32 defaults", test_unicode_string_defaults);
        run_test("unpack errors", test_unpack_errors);
        run_test("relaxed unpack", test_relaxed_unpack);
        run_test("pack buffer/counter/callback", test_pack_buffer_counter_callback_roundtrip);
        run_test("writer errors", test_writer_errors);
        run_test("double roundtrip", test_double_roundtrip);
    }
    printf("\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
