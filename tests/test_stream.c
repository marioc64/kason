#include <stdio.h>
#include <string.h>

#include "kason.h"

#define MAX_EVENTS 32
#define MAX_TEXT 128

typedef struct {
    int has_key;
    char key[MAX_TEXT];
    int type;
    int event;
    char data[MAX_TEXT];
} stream_event;

typedef struct {
    stream_event events[MAX_EVENTS];
    int event_count;
} stream_capture;

typedef struct {
    kason_stream nested_a;
    kason_stream nested_b;
    char scratch_a[16];
    char scratch_b[16];
    int active;
    stream_capture a_capture;
    stream_capture b_capture;
    int begin_count;
    int part_count;
    int end_count;
} nested_capture;

typedef struct {
    int call_count;
} stream_break_capture;

typedef struct {
    char *key_begin;
    char *value_begin;
    int call_count;
} stream_pointer_capture;

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            printf("    line %d: %s\n", __LINE__, #condition);                 \
            return 0;                                                           \
        }                                                                       \
    } while (0)

static int slice_length(const char *begin, const char *end)
{
    return begin != NULL && end != NULL && end >= begin
        ? (int)(end - begin + 1)
        : 0;
}

static void copy_slice(char *out, int out_size, const char *begin, const char *end)
{
    int length = slice_length(begin, end);

    if (length >= out_size) {
        length = out_size - 1;
    }
    if (length > 0) {
        memcpy(out, begin, (size_t)length);
    }
    out[length] = 0;
}

static int capture_stream_callback(kason_key *key, kason_stream_data *data, void *user_data)
{
    stream_capture *capture = (stream_capture *)user_data;

    if (capture->event_count < MAX_EVENTS) {
        stream_event *event = &capture->events[capture->event_count++];
        event->has_key = key != NULL;
        if (key != NULL) {
            copy_slice(event->key, sizeof(event->key), key->begin, key->end);
        } else {
            event->key[0] = 0;
        }
        event->type = data->type;
        event->event = data->event;
        copy_slice(event->data, sizeof(event->data), data->begin, data->end);
    }
    return KaSON_CALLBACK_CONTINUE;
}

static int break_stream_callback(kason_key *key, kason_stream_data *data, void *user_data)
{
    stream_break_capture *capture = (stream_break_capture *)user_data;

    (void)key;
    (void)data;
    capture->call_count++;
    return KaSON_CALLBACK_BREAK;
}

static int capture_stream_pointers(kason_key *key, kason_stream_data *data,
                                   void *user_data)
{
    stream_pointer_capture *capture = (stream_pointer_capture *)user_data;

    capture->key_begin = key != NULL ? key->begin : NULL;
    capture->value_begin = data->begin;
    capture->call_count++;
    return KaSON_CALLBACK_CONTINUE;
}

static int feed_one_byte(kason_stream *stream, char *input)
{
    int i;
    int length = (int)strlen(input);
    int result = KaSON_PARSE_RESULT_INCOMPLETE;

    for (i = 0; i < length; ++i) {
        result = kason_stream_feed(stream, input + i, 1);
        if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == KaSON_PARSE_RESULT_ERROR) {
            return result;
        }
    }
    return kason_stream_finish(stream);
}

static int test_stream_simple_object(void)
{
    char input[] = "{\"a\":1,\"b\":\"x\",\"c\":true}";
    char scratch[32];
    kason_stream stream;
    stream_capture capture = {0};

    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(feed_one_byte(&stream, input) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 3);
    CHECK(capture.events[0].has_key);
    CHECK(strcmp(capture.events[0].key, "a") == 0);
    CHECK(capture.events[0].type == KaSON_TYPE_NUMBER);
    CHECK(capture.events[0].event == KaSON_STREAM_EVENT_VALUE);
    CHECK(strcmp(capture.events[0].data, "1") == 0);
    CHECK(strcmp(capture.events[1].key, "b") == 0);
    CHECK(capture.events[1].type == KaSON_TYPE_STRING);
    CHECK(strcmp(capture.events[1].data, "x") == 0);
    CHECK(strcmp(capture.events[2].key, "c") == 0);
    CHECK(capture.events[2].type == KaSON_TYPE_TRUE);
    return 1;
}

static int test_stream_split_unicode(void)
{
    char input[] = "{\"a\":\"\\u0041\"}";
    char scratch[32];
    kason_stream stream;
    stream_capture capture = {0};

    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(feed_one_byte(&stream, input) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(capture.events[0].type == KaSON_TYPE_STRING);
    CHECK(strcmp(capture.events[0].data, "\\u0041") == 0);
    return 1;
}

static int test_stream_top_level_scalars(void)
{
    static const struct {
        const char *input;
        int type;
        const char *data;
    } cases[] = {
        {"\"text\"", KaSON_TYPE_STRING, "text"},
        {"-12.5e+2", KaSON_TYPE_NUMBER, "-12.5e+2"},
        {"true", KaSON_TYPE_TRUE, "true"},
        {"false", KaSON_TYPE_FALSE, "false"},
        {"null", KaSON_TYPE_NULL, "null"}
    };
    char scratch[32];
    kason_stream stream;
    stream_capture capture = {0};
    int i;

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_callback, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i) {
        char input[32];

        strcpy(input, cases[i].input);
        memset(&capture, 0, sizeof(capture));
        kason_stream_reset(&stream);
        CHECK(feed_one_byte(&stream, input) == KaSON_PARSE_RESULT_SUCCESS);
        CHECK(capture.event_count == 1);
        CHECK(!capture.events[0].has_key);
        CHECK(capture.events[0].event == KaSON_STREAM_EVENT_VALUE);
        CHECK(capture.events[0].type == cases[i].type);
        CHECK(strcmp(capture.events[0].data, cases[i].data) == 0);
    }
    return 1;
}

static int test_stream_escaped_keys_and_values(void)
{
    char input[] = "{\"a\\n\\u0042\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}";
    const char expected_value[] = "\\\"\\\\\\/\\b\\f\\n\\r\\t";
    char scratch[64];
    kason_stream stream;
    stream_capture capture = {0};

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_callback, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(feed_one_byte(&stream, input) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(strcmp(capture.events[0].key, "a\\n\\u0042") == 0);
    CHECK(capture.events[0].type == KaSON_TYPE_STRING);
    CHECK(strcmp(capture.events[0].data, expected_value) == 0);
    return 1;
}

static int test_stream_unicode_validation(void)
{
    char valid[] = "{\"a\":\"\\uD83D\\uDE00\",\"raw\":\"\xe2\x82\xac\"}";
    char invalid_escape[] = "{\"a\":\"\\uD800\"}";
    char invalid_raw[] = "{\"a\":\"\xed\xa0\x80\"}";
    char valid_nested[] = "{\"a\":{\"x\":\"\\uD83D\\uDE00\"}}";
    char invalid_nested_escape[] = "{\"a\":{\"x\":\"\\uD800\"}}";
    char invalid_nested_raw[] = "{\"a\":{\"x\":\"\xf4\x90\x80\x80\"}}";
    char scratch[64];
    kason_stream stream;
    stream_capture capture = {0};

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(feed_one_byte(&stream, valid) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 2);

    kason_stream_reset(&stream);
    CHECK(feed_one_byte(&stream, valid_nested) == KaSON_PARSE_RESULT_SUCCESS);
    kason_stream_reset(&stream);
    CHECK(feed_one_byte(&stream, invalid_escape) == KaSON_PARSE_RESULT_ERROR);
    kason_stream_reset(&stream);
    CHECK(feed_one_byte(&stream, invalid_raw) == KaSON_PARSE_RESULT_ERROR);
    kason_stream_reset(&stream);
    CHECK(feed_one_byte(&stream, invalid_nested_escape) == KaSON_PARSE_RESULT_ERROR);
    kason_stream_reset(&stream);
    CHECK(feed_one_byte(&stream, invalid_nested_raw) == KaSON_PARSE_RESULT_ERROR);
    return 1;
}

static int nested_root_callback(kason_key *key, kason_stream_data *data, void *user_data)
{
    nested_capture *capture = (nested_capture *)user_data;
    kason_stream *target = NULL;
    int result;

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN) {
        capture->begin_count++;
        if (key != NULL && slice_length(key->begin, key->end) == 1 && *key->begin == 'a') {
            capture->active = 'a';
        } else if (key != NULL && slice_length(key->begin, key->end) == 1 && *key->begin == 'b') {
            capture->active = 'b';
        } else {
            return KaSON_CALLBACK_CONTINUE;
        }
    } else if (data->event == KaSON_STREAM_EVENT_CONTAINER_PART) {
        capture->part_count++;
    } else if (data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        capture->end_count++;
    }

    if (capture->active == 'a') {
        target = &capture->nested_a;
    } else if (capture->active == 'b') {
        target = &capture->nested_b;
    }
    if (target != NULL) {
        result = kason_stream_feed(target,
                                  data->begin,
                                  slice_length(data->begin, data->end));
        if (result == KaSON_PARSE_RESULT_ERROR ||
                result == KaSON_PARSE_RESULT_ERROR_BUFFER_FULL ||
                result == KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK) {
            return KaSON_CALLBACK_BREAK;
        }
    }
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        if (target != NULL && kason_stream_finish(target) != KaSON_PARSE_RESULT_SUCCESS) {
            return KaSON_CALLBACK_BREAK;
        }
        capture->active = 0;
    }
    return KaSON_CALLBACK_CONTINUE;
}

static int test_stream_nested_objects(void)
{
    char input[] = "{\"a\":{\"p1\":1,\"p2\":2},\"b\":{\"p1\":2}}";
    char root_scratch[8];
    kason_stream root;
    nested_capture capture = {0};
    int i;

    CHECK(kason_stream_init(&capture.nested_a,
                           capture.scratch_a,
                           (int)sizeof(capture.scratch_a),
                           capture_stream_callback,
                           &capture.a_capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_init(&capture.nested_b,
                           capture.scratch_b,
                           (int)sizeof(capture.scratch_b),
                           capture_stream_callback,
                           &capture.b_capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_init(&root,
                           root_scratch,
                           (int)sizeof(root_scratch),
                           nested_root_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);

    for (i = 0; i < (int)strlen(input); ++i) {
        int result = kason_stream_feed(&root, input + i, 1);
        CHECK(result == KaSON_PARSE_RESULT_INCOMPLETE ||
              result == KaSON_PARSE_RESULT_SUCCESS);
    }
    CHECK(kason_stream_finish(&root) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.begin_count == 2);
    CHECK(capture.end_count == 2);
    CHECK(capture.part_count > 0);

    CHECK(capture.a_capture.event_count == 2);
    CHECK(strcmp(capture.a_capture.events[0].key, "p1") == 0);
    CHECK(strcmp(capture.a_capture.events[0].data, "1") == 0);
    CHECK(strcmp(capture.a_capture.events[1].key, "p2") == 0);
    CHECK(strcmp(capture.a_capture.events[1].data, "2") == 0);

    CHECK(capture.b_capture.event_count == 1);
    CHECK(strcmp(capture.b_capture.events[0].key, "p1") == 0);
    CHECK(strcmp(capture.b_capture.events[0].data, "2") == 0);
    return 1;
}

static int test_stream_array_events(void)
{
    char input[] = "[1,{\"a\":2},false]";
    char scratch[16];
    kason_stream stream;
    stream_capture capture = {0};

    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_feed(&stream, input, (int)strlen(input)) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_finish(&stream) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 5);
    CHECK(capture.events[0].type == KaSON_TYPE_NUMBER);
    CHECK(capture.events[0].event == KaSON_STREAM_EVENT_VALUE);
    CHECK(strcmp(capture.events[0].data, "1") == 0);
    CHECK(capture.events[1].type == KaSON_TYPE_OBJECT);
    CHECK(capture.events[1].event == KaSON_STREAM_EVENT_CONTAINER_BEGIN);
    CHECK(strcmp(capture.events[1].data, "{") == 0);
    CHECK(capture.events[2].event == KaSON_STREAM_EVENT_CONTAINER_PART);
    CHECK(strcmp(capture.events[2].data, "\"a\":2") == 0);
    CHECK(capture.events[3].event == KaSON_STREAM_EVENT_CONTAINER_END);
    CHECK(strcmp(capture.events[3].data, "}") == 0);
    CHECK(capture.events[4].type == KaSON_TYPE_FALSE);
    CHECK(capture.events[4].event == KaSON_STREAM_EVENT_VALUE);
    return 1;
}

static int test_stream_rejects_mismatched_container(void)
{
    char input[] = "{\"a\":[}]}";
    char scratch[16];
    kason_stream stream;
    stream_capture capture = {0};
    int result;

    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    result = kason_stream_feed(&stream, input, (int)strlen(input));
    CHECK(result == KaSON_PARSE_RESULT_ERROR);
    return 1;
}

static int test_stream_primitive_buffer_full(void)
{
    char input[] = "{\"a\":\"123456789\"}";
    char scratch[8];
    kason_stream stream;
    stream_capture capture = {0};
    int result;

    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    result = kason_stream_feed(&stream, input, (int)strlen(input));
    CHECK(result == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    CHECK(strcmp(capture.events[0].data, "123456789") == 0);

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    result = kason_stream_feed(&stream, input, 10);
    CHECK(result == KaSON_PARSE_RESULT_INCOMPLETE);
    result = kason_stream_feed(&stream, input + 10, (int)strlen(input) - 10);
    CHECK(result == KaSON_PARSE_RESULT_ERROR_BUFFER_FULL);
    return 1;
}

static int test_stream_zero_copy_fast_path(void)
{
    char input[] = "{\"key\":123}";
    char scratch[32];
    kason_stream stream;
    stream_pointer_capture capture = {0};
    int length = (int)strlen(input);

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_pointers, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_feed(&stream, input, length) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.call_count == 1);
    CHECK(capture.key_begin >= input && capture.key_begin < input + length);
    CHECK(capture.value_begin >= input && capture.value_begin < input + length);

    memset(&capture, 0, sizeof(capture));
    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_pointers, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_feed(&stream, input, 8) == KaSON_PARSE_RESULT_INCOMPLETE);
    CHECK(kason_stream_feed(&stream, input + 8, length - 8) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.call_count == 1);
    CHECK(capture.key_begin >= scratch && capture.key_begin < scratch + sizeof(scratch));
    CHECK(capture.value_begin >= scratch && capture.value_begin < scratch + sizeof(scratch));
    return 1;
}

static int test_stream_trailing_junk(void)
{
    char input[] = "{\"a\":1}x";
    char scratch[16];
    kason_stream stream;
    stream_capture capture = {0};
    int result;

    CHECK(kason_stream_init(&stream,
                           scratch,
                           (int)sizeof(scratch),
                           capture_stream_callback,
                           &capture) == KaSON_PARSE_RESULT_SUCCESS);
    result = kason_stream_feed(&stream, input, (int)strlen(input));
    CHECK(result == KaSON_PARSE_RESULT_ERROR);
    return 1;
}

static int test_stream_invalid_arguments_and_reset(void)
{
    char input[] = "{\"a\":1}";
    char scratch[16];
    kason_stream stream = {0};
    stream_capture capture = {0};

    kason_stream_reset(NULL);
    CHECK(kason_stream_init(NULL, scratch, (int)sizeof(scratch),
                           capture_stream_callback, &capture) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_stream_init(&stream, NULL, (int)sizeof(scratch),
                           capture_stream_callback, &capture) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_stream_init(&stream, scratch, 1,
                           capture_stream_callback, &capture) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           NULL, &capture) == KaSON_PARSE_RESULT_ERROR_NULL_CALLBACK);
    CHECK(kason_stream_feed(&stream, input, 1) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_stream_finish(&stream) == KaSON_PARSE_RESULT_ERROR);

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_feed(&stream, NULL, 1) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_stream_feed(&stream, input, -1) == KaSON_PARSE_RESULT_ERROR);
    CHECK(kason_stream_feed(&stream, NULL, 0) == KaSON_PARSE_RESULT_INCOMPLETE);
    CHECK(kason_stream_finish(&stream) == KaSON_PARSE_RESULT_ERROR);

    kason_stream_reset(&stream);
    CHECK(kason_stream_feed(&stream, input, (int)strlen(input)) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_finish(&stream) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.event_count == 1);
    return 1;
}

static int test_stream_rejects_malformed_json(void)
{
    static const char *const malformed[] = {
        "",
        " ",
        "{",
        "[",
        "\"",
        "\"\\q\"",
        "{\"a\":}",
        "{\"a\":1,}",
        "[1,]",
        "[,1]",
        "[1,,2]",
        "truth",
        "01",
        "1.",
        ".1",
        "1e",
        "-",
        "{\"a\n\":1}",
        "{\"a\":\"x\ny\"}",
        "{\"a\" 1}",
        "{\"a\":nullx}"
    };
    char scratch[64];
    kason_stream stream;
    stream_capture capture = {0};
    int i;

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           capture_stream_callback, &capture) ==
          KaSON_PARSE_RESULT_SUCCESS);
    for (i = 0; i < (int)(sizeof(malformed) / sizeof(malformed[0])); ++i) {
        char input[64];
        int result;

        strcpy(input, malformed[i]);
        memset(&capture, 0, sizeof(capture));
        kason_stream_reset(&stream);
        result = feed_one_byte(&stream, input);
        if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) != KaSON_PARSE_RESULT_ERROR) {
            printf("    accepted malformed stream: %s\n", malformed[i]);
            return 0;
        }
    }
    return 1;
}

static int test_stream_callback_break_and_reset(void)
{
    char input[] = "{\"a\":1,\"b\":2}";
    char ignored[] = "not JSON";
    char scratch[16];
    kason_stream stream;
    stream_break_capture capture = {0};

    CHECK(kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                           break_stream_callback, &capture) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_feed(&stream, input, (int)strlen(input)) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.call_count == 1);
    CHECK(kason_stream_feed(&stream, ignored, (int)strlen(ignored)) ==
          KaSON_PARSE_RESULT_SUCCESS);
    CHECK(kason_stream_finish(&stream) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.call_count == 1);

    kason_stream_reset(&stream);
    CHECK(kason_stream_feed(&stream, input, (int)strlen(input)) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(capture.call_count == 2);
    return 1;
}

static int stream_capture_equals(const stream_capture *left, const stream_capture *right)
{
    int i;

    if (left->event_count != right->event_count) {
        return 0;
    }
    for (i = 0; i < left->event_count; ++i) {
        const stream_event *a = &left->events[i];
        const stream_event *b = &right->events[i];

        if (a->has_key != b->has_key || a->type != b->type || a->event != b->event ||
                strcmp(a->key, b->key) != 0 || strcmp(a->data, b->data) != 0) {
            return 0;
        }
    }
    return 1;
}

static int parse_stream_at_split(char *input, int split, stream_capture *capture)
{
    char scratch[128];
    kason_stream stream;
    int length = (int)strlen(input);
    int result;

    result = kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                              capture_stream_callback, capture);
    if (result != KaSON_PARSE_RESULT_SUCCESS) {
        return result;
    }
    result = kason_stream_feed(&stream, input, split);
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == KaSON_PARSE_RESULT_ERROR) {
        return result;
    }
    result = kason_stream_feed(&stream, input + split, length - split);
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == KaSON_PARSE_RESULT_ERROR) {
        return result;
    }
    return kason_stream_finish(&stream);
}

static int test_stream_every_split_point(void)
{
    char input[] =
        "{\"escaped\\n\":\"\\u0041\",\"n\":-12.5e+2,\"t\":true,"
        "\"f\":false,\"z\":null}";
    stream_capture baseline = {0};
    int length = (int)strlen(input);
    int split;

    CHECK(parse_stream_at_split(input, length, &baseline) == KaSON_PARSE_RESULT_SUCCESS);
    CHECK(baseline.event_count == 5);
    for (split = 0; split <= length; ++split) {
        stream_capture capture = {0};

        CHECK(parse_stream_at_split(input, split, &capture) == KaSON_PARSE_RESULT_SUCCESS);
        CHECK(stream_capture_equals(&capture, &baseline));
    }
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
    run_test("stream simple object", test_stream_simple_object);
    run_test("stream split unicode", test_stream_split_unicode);
    run_test("stream top-level scalars", test_stream_top_level_scalars);
    run_test("stream escaped keys/values", test_stream_escaped_keys_and_values);
    run_test("stream unicode validation", test_stream_unicode_validation);
    run_test("stream nested objects", test_stream_nested_objects);
    run_test("stream array events", test_stream_array_events);
    run_test("stream mismatched container", test_stream_rejects_mismatched_container);
    run_test("stream primitive buffer full", test_stream_primitive_buffer_full);
    run_test("stream zero-copy fast path", test_stream_zero_copy_fast_path);
    run_test("stream trailing junk", test_stream_trailing_junk);
    run_test("stream invalid arguments", test_stream_invalid_arguments_and_reset);
    run_test("stream malformed JSON", test_stream_rejects_malformed_json);
    run_test("stream callback break/reset", test_stream_callback_break_and_reset);
    run_test("stream every split point", test_stream_every_split_point);

    printf("\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
