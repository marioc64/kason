#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kason.h"

static int callback(kason_key *key, kason_data *data, int count, void *user_data)
{
    (void)key;
    (void)data;
    (void)count;
    (void)user_data;
    return KaSON_CALLBACK_CONTINUE;
}

static int test_null_input(void)
{
    return kason_parse(NULL, callback, NULL) == KaSON_PARSE_RESULT_SUCCESS;
}

static int test_get_value_on_scalar(void)
{
    char input[] = "12";
    char key_text[] = "a";
    kason_key key = {key_text, key_text};
    kason_data value;

    return kason_get_value(input, input + 1, &key, &value) != 0;
}

static int test_truncated_unicode(void)
{
    char *input = (char *)malloc(3);
    int length;

    if (input == NULL) {
        return 1;
    }
    memcpy(input, "\\u1", 3);
    length = kason_strlen(input, input + 2);
    free(input);
    return length != KaSON_STRING_RESULT_ERROR;
}

static int test_bounded_whitespace(void)
{
    char *input = (char *)malloc(1);
    int result;

    if (input == NULL) {
        return 1;
    }
    input[0] = ' ';
    result = kason_parse_range(input, input, callback, NULL);
    free(input);
    return result == KaSON_PARSE_RESULT_SUCCESS;
}

static int test_get_value_null_key(void)
{
    char input[] = "{\"a\":1}";
    kason_data value;

    return kason_get_value(input, input + strlen(input) - 1, NULL, &value) != 0;
}

static int test_get_value_null_output(void)
{
    char input[] = "{\"a\":1}";
    char key_text[] = "a";
    kason_key key = {key_text, key_text};

    return kason_get_value(input, input + strlen(input) - 1, &key, NULL) != 0;
}

static int test_parse_array_null_output(void)
{
    char input[] = "1";

    return kason_parse_array(input, input, NULL, 1) >= 0;
}

static int test_deep_nesting(void)
{
    const int depth = KaSON_MAX_NESTING + 1;
    const size_t prefix_length = 5;
    size_t length = (size_t)depth * (prefix_length + 1) + 1;
    char *input = (char *)malloc(length + 1);
    char *ptr;
    int i;
    int result;

    if (input == NULL) {
        return 1;
    }
    ptr = input;
    for (i = 0; i < depth; ++i) {
        memcpy(ptr, "{\"a\":", prefix_length);
        ptr += prefix_length;
    }
    *ptr++ = '0';
    for (i = 0; i < depth; ++i) {
        *ptr++ = '}';
    }
    *ptr = 0;

    result = kason_parse(input, callback, NULL);
    free(input);
    return (result & KaSON_PARSE_RESULT_MAJOR_MASK) != KaSON_PARSE_RESULT_ERROR;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
                "usage: %s null-input|get-value-scalar|truncated-unicode|bounded-whitespace|"
                "get-value-null-key|get-value-null-output|parse-array-null-output|deep-nesting\n",
                argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "null-input") == 0) {
        return test_null_input();
    }
    if (strcmp(argv[1], "get-value-scalar") == 0) {
        return test_get_value_on_scalar();
    }
    if (strcmp(argv[1], "truncated-unicode") == 0) {
        return test_truncated_unicode();
    }
    if (strcmp(argv[1], "bounded-whitespace") == 0) {
        return test_bounded_whitespace();
    }
    if (strcmp(argv[1], "get-value-null-key") == 0) {
        return test_get_value_null_key();
    }
    if (strcmp(argv[1], "get-value-null-output") == 0) {
        return test_get_value_null_output();
    }
    if (strcmp(argv[1], "parse-array-null-output") == 0) {
        return test_parse_array_null_output();
    }
    if (strcmp(argv[1], "deep-nesting") == 0) {
        return test_deep_nesting();
    }
    fprintf(stderr, "unknown test: %s\n", argv[1]);
    return 2;
}
