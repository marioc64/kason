#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kason.h"

#define MAX_EVENTS 16

typedef struct {
    int has_key;
    kason_key key;
    kason_data data;
    int count;
} parse_event;

typedef struct {
    parse_event events[MAX_EVENTS];
    int event_count;
    int depth;
} parse_capture;

typedef struct {
    int call_count;
} break_capture;

typedef struct {
    int action;
    int begin_count;
    int end_count;
    int value_count;
    kason_data end_data;
} action_capture;

typedef struct {
    const kason_lookup_key *matched[MAX_EVENTS];
    kason_data data[MAX_EVENTS];
    int count;
} selected_capture;

static int failures;

static int capture_callback(kason_key *key, kason_data *data, int count, void *user_data)
{
    parse_capture *capture = (parse_capture *)user_data;

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN) {
        return capture->depth++ == 0 ? KaSON_ACTION_ENTER : KaSON_ACTION_CAPTURE;
    }
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        capture->depth--;
        if (capture->depth == 0) {
            return KaSON_CALLBACK_CONTINUE;
        }
    }

    if (capture->event_count < MAX_EVENTS) {
        parse_event *event = &capture->events[capture->event_count++];
        event->has_key = key != NULL;
        if (key != NULL) {
            event->key = *key;
        }
        event->data = *data;
        event->count = count;
    }
    return KaSON_CALLBACK_CONTINUE;
}

static int break_callback(kason_key *key, kason_data *data, int count, void *user_data)
{
    break_capture *capture = (break_capture *)user_data;

    (void)key;
    (void)data;
    (void)count;
    capture->call_count++;
    return KaSON_CALLBACK_BREAK;
}

static int action_callback(kason_key *key, kason_data *data, int count,
                           void *user_data)
{
    action_capture *capture = (action_capture *)user_data;

    (void)count;
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN) {
        capture->begin_count++;
        return key == NULL ? KaSON_ACTION_ENTER : capture->action;
    }
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        capture->end_count++;
        if (data->begin < data->end) {
            capture->end_data = *data;
        }
    } else {
        capture->value_count++;
    }
    return KaSON_CALLBACK_CONTINUE;
}

static int slice_length(const char *begin, const char *end)
{
    return begin != NULL && end != NULL && end >= begin
        ? (int)(end - begin + 1)
        : 0;
}

static int slice_equals(const char *begin, const char *end, const char *expected)
{
    int length = slice_length(begin, end);
    return length == (int)strlen(expected) &&
           memcmp(begin, expected, (size_t)length) == 0;
}

static int capture_selected_callback(const kason_lookup_key *matched_key,
                                     kason_data *data, int count, void *user_data)
{
    selected_capture *capture = (selected_capture *)user_data;

    (void)count;
    if (capture->count < MAX_EVENTS) {
        capture->matched[capture->count] = matched_key;
        capture->data[capture->count] = *data;
        capture->count++;
    }
    return KaSON_CALLBACK_CONTINUE;
}

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            printf("    line %d: %s\n", __LINE__, #condition);                 \
            return 0;                                                           \
        }                                                                       \
    } while (0)

static int parse_is_error(int result)
{
    return (result & KaSON_PARSE_RESULT_MAJOR_MASK) == KaSON_PARSE_RESULT_ERROR;
}

static int test_simple_object(void)
{
    char input[] = "{\"a\":1,\"b\":\"x\",\"c\":true,\"d\":false,\"e\":null}";
    parse_capture capture = {0};

    CHECK(kason_parse(input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 5);
    CHECK(capture.events[0].has_key);
    CHECK(slice_equals(capture.events[0].key.begin, capture.events[0].key.end, "a"));
    CHECK(capture.events[0].data.type == KaSON_TYPE_NUMBER);
    CHECK(slice_equals(capture.events[0].data.begin, capture.events[0].data.end, "1"));
    CHECK(capture.events[1].data.type == KaSON_TYPE_STRING);
    CHECK(slice_equals(capture.events[1].data.begin, capture.events[1].data.end, "x"));
    CHECK(capture.events[2].data.type == KaSON_TYPE_TRUE);
    CHECK(capture.events[3].data.type == KaSON_TYPE_FALSE);
    CHECK(capture.events[4].data.type == KaSON_TYPE_NULL);
    return 1;
}

static int test_empty_object(void)
{
    char input[] = "{}";
    parse_capture capture = {0};

    CHECK(kason_parse(input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 0);
    return 1;
}

static int test_container_actions(void)
{
    char input[] = "{\"a\":{\"b\":1}}";
    action_capture capture = {0};

    capture.action = KaSON_ACTION_CAPTURE;
    CHECK(kason_parse(input, action_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.begin_count == 2);
    CHECK(capture.end_count == 2);
    CHECK(capture.value_count == 0);
    CHECK(slice_equals(capture.end_data.begin, capture.end_data.end,
                       "{\"b\":1}"));

    memset(&capture, 0, sizeof(capture));
    capture.action = KaSON_ACTION_ENTER;
    CHECK(kason_parse(input, action_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.begin_count == 2);
    CHECK(capture.end_count == 2);
    CHECK(capture.value_count == 1);

    memset(&capture, 0, sizeof(capture));
    capture.action = KaSON_ACTION_SKIP;
    CHECK(kason_parse(input, action_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.begin_count == 2);
    CHECK(capture.end_count == 1);
    CHECK(capture.value_count == 0);

    memset(&capture, 0, sizeof(capture));
    capture.action = KaSON_ACTION_BREAK;
    CHECK(kason_parse(input, action_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.begin_count == 2);
    CHECK(capture.end_count == 0);
    return 1;
}

static int test_nested_values(void)
{
    char input[] = "{\"object\":{\"b\":2},\"array\":[1,2]}";
    parse_capture capture = {0};

    CHECK(kason_parse(input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 2);
    CHECK(capture.events[0].data.type == KaSON_TYPE_OBJECT);
    CHECK(slice_equals(capture.events[0].data.begin,
                       capture.events[0].data.end,
                       "{\"b\":2}"));
    CHECK(capture.events[1].data.type == KaSON_TYPE_ARRAY);
    CHECK(slice_equals(capture.events[1].data.begin,
                       capture.events[1].data.end,
                       "[1,2]"));
    CHECK(capture.events[1].count == 2);
    return 1;
}

static int test_leading_whitespace(void)
{
    char input[] = " \n\t{\"a\":1}";
    parse_capture capture = {0};

    CHECK(kason_parse(input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].has_key);
    CHECK(slice_equals(capture.events[0].key.begin, capture.events[0].key.end, "a"));
    CHECK(capture.events[0].data.type == KaSON_TYPE_NUMBER);
    return 1;
}

static int test_top_level_scalars(void)
{
    char string_input[] = "\"abc\"";
    char number_input[] = "-12.5e+2";
    char boolean_input[] = "true";
    char false_input[] = "false";
    char null_input[] = "null";
    parse_capture capture = {0};

    CHECK(kason_parse(string_input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].data.type == KaSON_TYPE_STRING);
    CHECK(slice_equals(capture.events[0].data.begin, capture.events[0].data.end, "abc"));

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_parse(number_input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].data.type == KaSON_TYPE_NUMBER);

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_parse(boolean_input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].data.type == KaSON_TYPE_TRUE);

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_parse(false_input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].data.type == KaSON_TYPE_FALSE);

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_parse(null_input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].data.type == KaSON_TYPE_NULL);
    return 1;
}

static int test_valid_json_matrix(void)
{
    static const char *const valid[] = {
        "{\"a\":1} \n\t",
        "{ \"a\" : [ 1 , 2 ] }",
        "{\"a\":[],\"b\":{}}",
        "[1,\"x\",true,false,null,{},[]]",
        "\"\\/\\b\\f\\r\"",
        "{\"a\":\"\\u0041\"}",
        "0",
        "-0",
        "1e10",
        "1E-10"
    };
    int i;

    for (i = 0; i < (int)(sizeof(valid) / sizeof(valid[0])); ++i) {
        char input[128];
        parse_capture capture = {0};
        int result;

        CHECK(strlen(valid[i]) < sizeof(input));
        strcpy(input, valid[i]);
        result = kason_parse(input, capture_callback, &capture);
        if (result != KaSON_PARSE_RESULT_SUCCESS) {
            printf("    rejected valid input: %s\n", valid[i]);
            return 0;
        }
    }
    return 1;
}

static int test_nested_empty_values(void)
{
    char input[] = "{\"array\":[],\"object\":{}}";
    parse_capture capture = {0};

    CHECK(kason_parse(input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 2);
    CHECK(capture.events[0].data.type == KaSON_TYPE_ARRAY);
    CHECK(capture.events[0].count == 0);
    CHECK(capture.events[1].data.type == KaSON_TYPE_OBJECT);
    CHECK(slice_equals(capture.events[1].data.begin,
                       capture.events[1].data.end,
                       "{}"));
    return 1;
}

static int test_empty_array_count(void)
{
    char input[] = "[]";
    parse_capture capture = {0};

    CHECK(kason_parse(input, capture_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 0);
    return 1;
}

static int test_array_helper(void)
{
    char short_input[] = "1,2";
    char longer_input[] = "11,22";
    kason_data elements[4];
    int count;

    count = kason_parse_array(short_input,
                             short_input + strlen(short_input) - 1,
                             elements,
                             4);
    CHECK(count == 2);
    CHECK(elements[0].type == KaSON_TYPE_NUMBER);
    CHECK(slice_equals(elements[0].begin, elements[0].end, "1"));
    CHECK(slice_equals(elements[1].begin, elements[1].end, "2"));

    count = kason_parse_array(longer_input,
                             longer_input + strlen(longer_input) - 1,
                             elements,
                             4);
    CHECK(count == 2);
    CHECK(slice_equals(elements[0].begin, elements[0].end, "11"));
    CHECK(slice_equals(elements[1].begin, elements[1].end, "22"));
    return 1;
}

static int test_array_helper_edges(void)
{
    char input[] = "1,\"x\",null";
    char limited_input[] = "1,2,3";
    char trailing_comma[] = "1,";
    char missing_element[] = "1,,2";
    kason_data elements[4];
    int count;

    count = kason_parse_array(input,
                             input + strlen(input) - 1,
                             elements,
                             4);
    CHECK(count == 3);
    CHECK(elements[0].type == KaSON_TYPE_NUMBER);
    CHECK(elements[1].type == KaSON_TYPE_STRING);
    CHECK(slice_equals(elements[1].begin, elements[1].end, "x"));
    CHECK(elements[2].type == KaSON_TYPE_NULL);

    count = kason_parse_array(limited_input,
                             limited_input + strlen(limited_input) - 1,
                             elements,
                             2);
    CHECK(count == 2);
    CHECK(slice_equals(elements[0].begin, elements[0].end, "1"));
    CHECK(slice_equals(elements[1].begin, elements[1].end, "2"));

    count = kason_parse_array(limited_input,
                             limited_input + strlen(limited_input) - 1,
                             elements,
                             0);
    CHECK(count == 0);

    CHECK(kason_parse_array(trailing_comma,
                           trailing_comma + strlen(trailing_comma) - 1,
                           elements,
                           4) < 0);
    CHECK(kason_parse_array(missing_element,
                           missing_element + strlen(missing_element) - 1,
                           elements,
                           4) < 0);
    return 1;
}

static int test_parse_range_without_null_terminator(void)
{
    char valid[] = {'x', 'x', '{', '"', 'a', '"', ':', '1', '}', 'y'};
    char invalid[] = {'x', 'x', '{', '"', 'a', '"', ':', '1'};
    parse_capture capture = {0};

    CHECK(kason_parse_range(valid + 2,
                           valid + 8,
                           capture_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].has_key);
    CHECK(slice_equals(capture.events[0].key.begin,
                       capture.events[0].key.end,
                       "a"));
    CHECK(capture.events[0].data.type == KaSON_TYPE_NUMBER);

    memset(&capture, 0, sizeof(capture));
    CHECK(parse_is_error(kason_parse_range(invalid + 2,
                                          invalid + 7,
                                          capture_callback,
                                          &capture)));
    return 1;
}

static int test_get_value(void)
{
    char input[] = "{\"target\":42,\"other\":\"x\"}";
    char escaped_input[] = "{\"a\\nb\":true}";
    char target[] = "target";
    char missing[] = "missing";
    char escaped_key[] = {'a', '\n', 'b'};
    kason_key key = {target, target + strlen(target) - 1};
    kason_key missing_key = {missing, missing + strlen(missing) - 1};
    kason_key escaped = {escaped_key, escaped_key + sizeof(escaped_key) - 1};
    kason_data value;

    CHECK(kason_get_value(input, input + strlen(input) - 1, &key, &value) == 1);
    CHECK(value.type == KaSON_TYPE_NUMBER);
    CHECK(slice_equals(value.begin, value.end, "42"));

    CHECK(kason_get_value(input,
                         input + strlen(input) - 1,
                         &missing_key,
                         &value) == 0);

    CHECK(kason_get_value(escaped_input,
                         escaped_input + strlen(escaped_input) - 1,
                         &escaped,
                         &value) == 1);
    CHECK(value.type == KaSON_TYPE_TRUE);
    return 1;
}

static int test_selected_keys(void)
{
    static const char answer[] = "answer";
    static const char collision[] = "xxxxxx";
    static const char line_break[] = {'l', 'i', 'n', 'e', '\n'};
    char input[] = "{\"ignored\":0,\"answer\":42,\"line\\n\":7}";
    kason_lookup_key slots[8];
    kason_lookup_table table;
    selected_capture capture = {0};
    uint64_t value;

    CHECK(kason_lookup_table_init(&table, slots, 8) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_lookup_table_add(&table, collision, (int)sizeof(collision) - 1) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_lookup_table_add(&table, answer, (int)sizeof(answer) - 1) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_lookup_table_add(&table, line_break, (int)sizeof(line_break)) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(table.count == 3);
    CHECK(kason_parse_selected(input, &table,
                              capture_selected_callback, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.count == 2);
    CHECK(capture.matched[0]->value == answer);
    CHECK(capture.matched[0]->length == 6);
    CHECK(capture.matched[0]->hash != 0);
    CHECK(capture.matched[1]->value == line_break);
    CHECK(kason_value_to_uint64(capture.data[0].type, capture.data[0].begin,
                               capture.data[0].end, &value) == KaSON_CONVERT_SUCCESS);
    CHECK(value == 42);
    CHECK(kason_value_to_uint64(capture.data[1].type, capture.data[1].begin,
                               capture.data[1].end, &value) == KaSON_CONVERT_SUCCESS);
    CHECK(value == 7);

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_parse_range_selected(input, input + sizeof(input) - 2,
                                    &table, capture_selected_callback,
                                    &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.count == 2);
    CHECK(kason_lookup_key_init(NULL, answer, 6) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_key_init(&slots[0], NULL, 6) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_key_init(&slots[0], answer, -1) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_init(NULL, slots, 8) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_init(&table, NULL, 8) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_init(&table, slots, 0) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_init(&table, slots, 8) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_parse_selected(input, &table,
                              capture_selected_callback, &capture) ==
          KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_add(&table, answer, 6) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_lookup_table_add(&table, answer, 6) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_add(&table, NULL, 6) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_lookup_table_add(&table, answer, -1) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_parse_selected(input, &table, NULL, &capture) ==
          KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK);

    /* Force a full hash collision and verify decoded-key comparison. */
    CHECK(kason_lookup_table_init(&table, slots, 8) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_lookup_key_init(&slots[0], collision,
                               (int)sizeof(collision) - 1) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_lookup_key_init(&slots[1], answer, (int)sizeof(answer) - 1) ==
          KaSON_PARSE_RESULT_SUCCESS);
    slots[0].hash = slots[1].hash;
    {
        int index = (int)(slots[1].hash % 8U);
        kason_lookup_key first = slots[0];
        kason_lookup_key second = slots[1];
        memset(slots, 0, sizeof(slots));
        slots[index] = first;
        slots[(index + 1) % 8] = second;
        table.count = 2;
    }
    memset(&capture, 0, sizeof(capture));
    CHECK(kason_parse_selected(input, &table,
                              capture_selected_callback, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.count == 1);
    CHECK(capture.matched[0]->value == answer);
    return 1;
}

static int test_primitive_conversions(void)
{
    const char positive[] = "42";
    const char negative[] = "-42";
    const char int64_maximum[] = "9223372036854775807";
    const char int64_minimum[] = "-9223372036854775808";
    const char int64_underflow[] = "-9223372036854775809";
    const char uint64_maximum[] = "18446744073709551615";
    const char overflow[] = "18446744073709551616";
    const char fraction[] = "12.5";
    const char exponent[] = "-12.5e+2";
    const char double_maximum[] = "1.7976931348623157e308";
    const char double_overflow[] = "1.7976931348623159e308";
    const char leading_zero[] = "01";
    const char huge[] = "1e999999";
    const char tiny[] = "1e-999999";
    int int_value = 7;
    int64_t int64_value = 7;
    uint64_t uint64_value = 7;
    double double_value = 7.0;

    CHECK(kason_value_to_int(KaSON_TYPE_NUMBER, positive,
                            positive + sizeof(positive) - 2,
                            &int_value) == KaSON_CONVERT_SUCCESS);
    CHECK(int_value == 42);
    CHECK(kason_value_to_int(KaSON_TYPE_NUMBER, negative,
                            negative + sizeof(negative) - 2,
                            &int_value) == KaSON_CONVERT_SUCCESS);
    CHECK(int_value == -42);

    CHECK(kason_value_to_int64(KaSON_TYPE_NUMBER, int64_maximum,
                              int64_maximum + sizeof(int64_maximum) - 2,
                              &int64_value) == KaSON_CONVERT_SUCCESS);
    CHECK(int64_value == INT64_MAX);
    CHECK(kason_value_to_int64(KaSON_TYPE_NUMBER, int64_minimum,
                              int64_minimum + sizeof(int64_minimum) - 2,
                              &int64_value) == KaSON_CONVERT_SUCCESS);
    CHECK(int64_value == INT64_MIN);

    CHECK(kason_value_to_uint64(KaSON_TYPE_NUMBER, uint64_maximum,
                               uint64_maximum + sizeof(uint64_maximum) - 2,
                               &uint64_value) == KaSON_CONVERT_SUCCESS);
    CHECK(uint64_value == UINT64_MAX);

    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, fraction,
                               fraction + sizeof(fraction) - 2,
                               &double_value) == KaSON_CONVERT_SUCCESS);
    CHECK(double_value == 12.5);
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, exponent,
                               exponent + sizeof(exponent) - 2,
                               &double_value) == KaSON_CONVERT_SUCCESS);
    CHECK(double_value == -1250.0);
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, positive,
                               positive + sizeof(positive) - 2,
                               &double_value) == KaSON_CONVERT_SUCCESS);
    CHECK(double_value == 42.0);
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, double_maximum,
                               double_maximum + sizeof(double_maximum) - 2,
                               &double_value) == KaSON_CONVERT_SUCCESS);
    CHECK(double_value == DBL_MAX);

    int_value = 7;
    CHECK(kason_value_to_int(KaSON_TYPE_NUMBER, fraction,
                            fraction + sizeof(fraction) - 2,
                            &int_value) == KaSON_CONVERT_ERROR);
    CHECK(int_value == 7);
    CHECK(kason_value_to_int(KaSON_TYPE_NUMBER, uint64_maximum,
                            uint64_maximum + sizeof(uint64_maximum) - 2,
                            &int_value) == KaSON_CONVERT_RANGE);
    CHECK(int_value == 7);
    CHECK(kason_value_to_int64(KaSON_TYPE_NUMBER, overflow,
                              overflow + sizeof(overflow) - 2,
                              &int64_value) == KaSON_CONVERT_RANGE);
    CHECK(kason_value_to_int64(KaSON_TYPE_NUMBER, int64_underflow,
                              int64_underflow + sizeof(int64_underflow) - 2,
                              &int64_value) == KaSON_CONVERT_RANGE);
    CHECK(kason_value_to_uint64(KaSON_TYPE_NUMBER, negative,
                               negative + sizeof(negative) - 2,
                               &uint64_value) == KaSON_CONVERT_RANGE);
    CHECK(kason_value_to_uint64(KaSON_TYPE_NUMBER, leading_zero,
                               leading_zero + sizeof(leading_zero) - 2,
                               &uint64_value) == KaSON_CONVERT_ERROR);
    double_value = 7.0;
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, double_overflow,
                               double_overflow + sizeof(double_overflow) - 2,
                               &double_value) == KaSON_CONVERT_RANGE);
    CHECK(double_value == 7.0);
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, huge,
                               huge + sizeof(huge) - 2,
                               &double_value) == KaSON_CONVERT_RANGE);
    CHECK(double_value == 7.0);
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, tiny,
                               tiny + sizeof(tiny) - 2,
                               &double_value) == KaSON_CONVERT_RANGE);
    CHECK(double_value == 7.0);
    CHECK(kason_value_to_double(KaSON_TYPE_NUMBER, leading_zero,
                               leading_zero + sizeof(leading_zero) - 2,
                               &double_value) == KaSON_CONVERT_ERROR);
    CHECK(double_value == 7.0);
    CHECK(kason_value_to_int(KaSON_TYPE_NUMBER, positive,
                            positive + sizeof(positive) - 2,
                            NULL) == KaSON_CONVERT_ERROR);
    return 1;
}

static int test_callback_break(void)
{
    char input[] = "{\"a\":1,\"b\":2}";
    break_capture capture = {0};

    CHECK(kason_parse(input, break_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.call_count == 1);
    return 1;
}

static int test_string_helpers(void)
{
    const char encoded[] = "a\\n\\t\\\\\\\"";
    const char expected[] = {'a', '\n', '\t', '\\', '"'};
    const char unicode_a[] = "\\u0041";
    const char plain_a[] = "A";
    char output[sizeof(expected)];

    CHECK(kason_strlen(encoded, encoded + strlen(encoded) - 1) == (int)sizeof(expected));
    CHECK(kason_strcpy(encoded,
                      encoded + strlen(encoded) - 1,
                      output,
                      (int)sizeof(output)) == (int)sizeof(expected));
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CHECK(kason_strcmp(unicode_a,
                      unicode_a + sizeof(unicode_a) - 2,
                      plain_a,
                      plain_a) == 0);
    return 1;
}

static int test_unicode_string_helpers(void)
{
    const char encoded[] = "A\\u00E9\\u20AC\\uD83D\\uDE00";
    const char raw[] = {'A', (char)0xc3, (char)0xa9, (char)0xe2, (char)0x82,
                        (char)0xac, (char)0xf0, (char)0x9f, (char)0x98, (char)0x80};
    const uint16_t expected16[] = {'A', 0x00e9, 0x20ac, 0xd83d, 0xde00};
    const uint32_t expected32[] = {'A', 0x00e9, 0x20ac, 0x1f600};
    const char supplementary[] = "\\uD83D\\uDE00";
    const char invalid[] = "\\uD800";
    const char nul[] = "\\u0000";
    char utf8[sizeof(raw)];
    char small8[3] = {1, 2, 3};
    uint16_t utf16[sizeof(expected16) / sizeof(expected16[0])];
    uint16_t small16[1] = {0x1234};
    uint32_t utf32[sizeof(expected32) / sizeof(expected32[0])];
    char nul_output = 1;

    CHECK(kason_strlen(encoded, encoded + sizeof(encoded) - 2) == (int)sizeof(raw));
    CHECK(kason_strcpy(encoded, encoded + sizeof(encoded) - 2,
                      utf8, (int)sizeof(utf8)) == (int)sizeof(raw));
    CHECK(memcmp(utf8, raw, sizeof(raw)) == 0);
    CHECK(kason_strcpy_utf16(encoded, encoded + sizeof(encoded) - 2,
                            NULL, 0) == (int)(sizeof(expected16) / sizeof(expected16[0])));
    CHECK(kason_strcpy_utf16(encoded, encoded + sizeof(encoded) - 2,
                            utf16, (int)(sizeof(utf16) / sizeof(utf16[0]))) ==
          (int)(sizeof(expected16) / sizeof(expected16[0])));
    CHECK(memcmp(utf16, expected16, sizeof(expected16)) == 0);
    CHECK(kason_strcpy_utf32(encoded, encoded + sizeof(encoded) - 2,
                            utf32, (int)(sizeof(utf32) / sizeof(utf32[0]))) ==
          (int)(sizeof(expected32) / sizeof(expected32[0])));
    CHECK(memcmp(utf32, expected32, sizeof(expected32)) == 0);
    CHECK(kason_strcmp(encoded, encoded + sizeof(encoded) - 2,
                      raw, raw + sizeof(raw) - 1) == 0);

    CHECK(kason_strcpy(supplementary, supplementary + sizeof(supplementary) - 2,
                      small8, (int)sizeof(small8)) == 0);
    CHECK(small8[0] == 1 && small8[1] == 2 && small8[2] == 3);
    CHECK(kason_strcpy_utf16(supplementary,
                            supplementary + sizeof(supplementary) - 2,
                            small16, 1) == 0);
    CHECK(small16[0] == 0x1234);
    CHECK(kason_strlen(invalid, invalid + sizeof(invalid) - 2) == KaSON_STRING_RESULT_ERROR);
    CHECK(kason_strcpy_utf16(invalid, invalid + sizeof(invalid) - 2,
                            utf16, (int)(sizeof(utf16) / sizeof(utf16[0]))) ==
          KaSON_STRING_RESULT_ERROR);
    CHECK(kason_strcpy_utf32(invalid, invalid + sizeof(invalid) - 2,
                            utf32, (int)(sizeof(utf32) / sizeof(utf32[0]))) ==
          KaSON_STRING_RESULT_ERROR);
    CHECK(kason_strcpy(nul, nul + sizeof(nul) - 2, &nul_output, 1) == 1);
    CHECK(nul_output == 0);
    return 1;
}

static int test_unicode_boundaries(void)
{
    const char encoded[] =
        "\\u007F\\u0080\\u07FF\\u0800\\uD7FF\\uE000\\uFFFF"
        "\\uD800\\uDC00\\uDBFF\\uDFFF";
    const uint32_t expected[] = {
        0x007f, 0x0080, 0x07ff, 0x0800, 0xd7ff, 0xe000, 0xffff,
        0x10000, 0x10ffff
    };
    uint32_t output[sizeof(expected) / sizeof(expected[0])];

    CHECK(kason_strcpy_utf32(encoded, encoded + sizeof(encoded) - 2,
                            output, (int)(sizeof(output) / sizeof(output[0]))) ==
          (int)(sizeof(expected) / sizeof(expected[0])));
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    return 1;
}

static int test_string_helper_edges(void)
{
    static const char *const malformed[] = {
        "\\",
        "\\q",
        "\\u12",
        "\\uZZZZ",
        "\\uDC00",
        "\x80",
        "\xc0\xaf",
        "\xe2\x82",
        "\xed\xa0\x80",
        "\xf4\x90\x80\x80"
    };
    const char all_escapes[] = "\\\"\\\\\\/\\b\\f\\n\\r\\t";
    const char expected[] = {'"', '\\', '/', '\b', '\f', '\n', '\r', '\t'};
    const char short_output_input[] = "A\\u20ACB";
    const char less[] = "a";
    const char greater[] = "b";
    const char longer[] = "aa";
    const char escaped_a[] = "\\u0061";
    char output[sizeof(expected)] = {0};
    uint16_t output16[2] = {0};
    uint32_t output32[2] = {0};
    char empty_marker = 0;
    int i;

    CHECK(kason_strcpy(all_escapes, all_escapes + sizeof(all_escapes) - 2,
                      output, (int)sizeof(output)) == (int)sizeof(expected));
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);

    CHECK(kason_strlen(&empty_marker + 1, &empty_marker) == 0);
    CHECK(kason_strcpy(short_output_input,
                      short_output_input + sizeof(short_output_input) - 2,
                      output, 2) == 1);
    CHECK(output[0] == 'A');
    CHECK(kason_strcpy_utf16(short_output_input,
                            short_output_input + sizeof(short_output_input) - 2,
                            output16, 1) == 1);
    CHECK(output16[0] == 'A');
    CHECK(kason_strcpy_utf32(short_output_input,
                            short_output_input + sizeof(short_output_input) - 2,
                            output32, 1) == 1);
    CHECK(output32[0] == 'A');

    CHECK(kason_strcpy(NULL, NULL, output, (int)sizeof(output)) == KaSON_STRING_RESULT_ERROR);
    CHECK(kason_strcpy(all_escapes, all_escapes, output, -1) == KaSON_STRING_RESULT_ERROR);
    CHECK(kason_strcpy_utf16(all_escapes, all_escapes, output16, -1) == KaSON_STRING_RESULT_ERROR);
    CHECK(kason_strcpy_utf32(all_escapes, all_escapes, output32, -1) == KaSON_STRING_RESULT_ERROR);

    for (i = 0; i < (int)(sizeof(malformed) / sizeof(malformed[0])); ++i) {
        const char *end = malformed[i] + strlen(malformed[i]) - 1;

        CHECK(kason_strlen(malformed[i], end) == KaSON_STRING_RESULT_ERROR);
        CHECK(kason_strcpy(malformed[i], end, output, (int)sizeof(output)) ==
              KaSON_STRING_RESULT_ERROR);
        CHECK(kason_strcpy_utf16(malformed[i], end, output16, 2) ==
              KaSON_STRING_RESULT_ERROR);
        CHECK(kason_strcpy_utf32(malformed[i], end, output32, 2) ==
              KaSON_STRING_RESULT_ERROR);
    }

    CHECK(kason_strcmp(less, less, greater, greater) < 0);
    CHECK(kason_strcmp(greater, greater, less, less) > 0);
    CHECK(kason_strcmp(less, less, longer, longer + 1) < 0);
    CHECK(kason_strcmp(longer, longer + 1, less, less) > 0);
    CHECK(kason_strcmp(less, less, escaped_a, escaped_a + sizeof(escaped_a) - 2) == 0);
    return 1;
}

static int test_parser_unicode_validation(void)
{
    static const char *const malformed[] = {
        "{\"a\":\"\\uD800\"}",
        "{\"a\":\"\\uDC00\"}",
        "{\"a\":\"\\uD800\\u0041\"}",
        "{\"a\":\"\\uD800x\"}",
        "{\"a\":\"\x80\"}",
        "{\"a\":\"\xc0\xaf\"}",
        "{\"a\":\"\xe2\x82\"}",
        "{\"a\":\"\xed\xa0\x80\"}",
        "{\"a\":\"\xf4\x90\x80\x80\"}"
    };
    char valid[] = "{\"\\u00E9\":\"\\uD83D\\uDE00\",\"raw\":\"\xe2\x82\xac\"}";
    char key_text[] = {(char)0xc3, (char)0xa9};
    kason_key key = {key_text, key_text + sizeof(key_text) - 1};
    kason_data value;
    int i;

    CHECK(kason_parse(valid, capture_callback, &(parse_capture){0}) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_get_value(valid, valid + strlen(valid) - 1, &key, &value) == 1);
    CHECK(value.type == KaSON_TYPE_STRING);
    for (i = 0; i < (int)(sizeof(malformed) / sizeof(malformed[0])); ++i) {
        char input[32];
        parse_capture capture = {0};
        size_t length = strlen(malformed[i]);

        memcpy(input, malformed[i], length + 1);
        CHECK(parse_is_error(kason_parse(input, capture_callback, &capture)));
    }
    return 1;
}

static int test_null_callback(void)
{
    char input[] = "{}";
    CHECK(kason_parse(input, NULL, NULL) == KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK);
    return 1;
}

static int test_rejects_malformed_json(void)
{
    static const char *const malformed[] = {
        "",
        " ",
        "{\"a\":1",
        "{\"a\":\"x}",
        "{\"a\":truth}",
        "{\"a\":nullx}",
        "{\"a\":1x}",
        "{\"a\":1,}",
        "{\"a\":1}xyz",
        "xyz",
        "[1,]",
        "[truth]",
        "{\"a\":[truth]}",
        "{\"a\":{\"b\":truth}}",
        "{\"a\":[1}}",
        "{\"a\":\"\\q\"}",
        "{\"a\":\"x\ny\"}",
        "01",
        "1.",
        ".1",
        "1e",
        "-",
        "[,1]",
        "[1,,2]",
        "{\"a\":}",
        "{\"a\",1}",
        "{\"a\":1,,\"b\":2}",
        "\"\\u12\"",
        "\"\\u12xz\""
    };
    int i;

    for (i = 0; i < (int)(sizeof(malformed) / sizeof(malformed[0])); ++i) {
        char input[64];
        parse_capture capture = {0};
        int result;

        strcpy(input, malformed[i]);
        result = kason_parse(input, capture_callback, &capture);
        if (!parse_is_error(result)) {
            printf("    accepted malformed input: %s\n", malformed[i]);
            return 0;
        }
    }
    return 1;
}

static int test_rejects_trailing_root_array_values(void)
{
    static const char *const malformed[] = {
        "[],0]",
        "[1],[2]",
        "[null],false]",
        "[] , null ]"
    };
    int i;

    for (i = 0; i < (int)(sizeof(malformed) / sizeof(malformed[0])); ++i) {
        char input[32];
        parse_capture capture = {0};

        CHECK(strlen(malformed[i]) < sizeof(input));
        strcpy(input, malformed[i]);
        CHECK(parse_is_error(kason_parse(input, capture_callback, &capture)));
    }
    return 1;
}

static int test_scalar_trailing_whitespace(void)
{
    static const char *const valid[] = {
        "0 ",
        "true\n",
        "false\t",
        "null\r"
    };
    int i;

    for (i = 0; i < (int)(sizeof(valid) / sizeof(valid[0])); ++i) {
        char input[16];
        parse_capture capture = {0};
        size_t length = strlen(valid[i]);

        CHECK(length < sizeof(input));
        memcpy(input, valid[i], length + 1);
        CHECK(kason_parse(input, capture_callback, &capture) ==
              KaSON_PARSE_RESULT_SUCCESS);

        memset(&capture, 0, sizeof(capture));
        CHECK(kason_parse_range(input, input + length - 1,
                               capture_callback, &capture) ==
              KaSON_PARSE_RESULT_SUCCESS);
    }
    return 1;
}

static int test_parse_container(void)
{
    char input[] = "{\"object\":{\"a\":1},\"array\":[2,3],\"empty\":[],\"primitive\":4}";
    parse_capture parent = {0};
    parse_capture child = {0};

    CHECK(kason_parse(input, capture_callback, &parent) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(parent.event_count == 4);

    CHECK(kason_parse_container(&parent.events[0].data, capture_callback, &child) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(child.event_count == 1);
    CHECK(slice_equals(child.events[0].key.begin, child.events[0].key.end, "a"));

    memset(&child, 0, sizeof(child));
    CHECK(kason_parse_container(&parent.events[1].data, capture_callback, &child) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(child.event_count == 2);
    CHECK(slice_equals(child.events[0].data.begin, child.events[0].data.end, "2"));
    CHECK(slice_equals(child.events[1].data.begin, child.events[1].data.end, "3"));

    memset(&child, 0, sizeof(child));
    CHECK(kason_parse_container(&parent.events[2].data, capture_callback, &child) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(child.event_count == 0);
    CHECK(kason_parse_container(&parent.events[3].data, capture_callback, &child) ==
          KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_parse_container(&parent.events[0].data, NULL, NULL) ==
          KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK);
    return 1;
}

static char *make_nested_object(int depth)
{
    static const char prefix[] = "{\"a\":";
    size_t length = (size_t)depth * ((sizeof(prefix) - 1) + 1) + 1;
    char *input = (char *)malloc(length + 1);
    char *ptr = input;
    int i;

    if (input == NULL) {
        return NULL;
    }
    for (i = 0; i < depth; ++i) {
        memcpy(ptr, prefix, sizeof(prefix) - 1);
        ptr += sizeof(prefix) - 1;
    }
    *ptr++ = '0';
    for (i = 0; i < depth; ++i) {
        *ptr++ = '}';
    }
    *ptr = 0;
    return input;
}

static int test_nesting_boundaries(void)
{
    char *at_limit = make_nested_object(KaSON_MAX_NESTING);
    char *over_limit = make_nested_object(KaSON_MAX_NESTING + 1);
    parse_capture capture = {0};
    int at_result;
    int over_result;

    CHECK(at_limit != NULL);
    CHECK(over_limit != NULL);
    at_result = kason_parse(at_limit, capture_callback, &capture);
    over_result = kason_parse(over_limit, capture_callback, &capture);
    free(at_limit);
    free(over_limit);

    CHECK(at_result == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(parse_is_error(over_result));
    return 1;
}

static void run_test(const char *name, int (*test_function)(void))
{
    printf("%-32s", name);
    if (test_function()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        failures++;
    }
}

int main(void)
{
    run_test("simple object", test_simple_object);
    run_test("empty object", test_empty_object);
    run_test("container actions", test_container_actions);
    run_test("nested values", test_nested_values);
    run_test("leading whitespace", test_leading_whitespace);
    run_test("top-level scalars", test_top_level_scalars);
    run_test("valid JSON matrix", test_valid_json_matrix);
    run_test("nested empty values", test_nested_empty_values);
    run_test("empty array count", test_empty_array_count);
    run_test("array helper", test_array_helper);
    run_test("array helper edges", test_array_helper_edges);
    run_test("parse range no terminator", test_parse_range_without_null_terminator);
    run_test("get value", test_get_value);
    run_test("selected keys", test_selected_keys);
    run_test("primitive conversions", test_primitive_conversions);
    run_test("callback break", test_callback_break);
    run_test("string helpers", test_string_helpers);
    run_test("unicode string helpers", test_unicode_string_helpers);
    run_test("unicode boundaries", test_unicode_boundaries);
    run_test("string helper edges", test_string_helper_edges);
    run_test("parser unicode validation", test_parser_unicode_validation);
    run_test("null callback", test_null_callback);
    run_test("reject malformed JSON", test_rejects_malformed_json);
    run_test("reject root array tail", test_rejects_trailing_root_array_values);
    run_test("scalar trailing whitespace", test_scalar_trailing_whitespace);
    run_test("parse container", test_parse_container);
    run_test("nesting boundaries", test_nesting_boundaries);

    printf("\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
