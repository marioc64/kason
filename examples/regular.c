#include <stdio.h>
#include <string.h>

#include "kason.h"

static int key_is(const kason_key *key, const char *expected)
{
    size_t length;

    if (key == NULL)
        return 0;
    length = (size_t)(key->end - key->begin + 1);
    return strlen(expected) == length &&
           memcmp(key->begin, expected, length) == 0;
}

static int on_value(kason_key *key, kason_data *data,
                    int count, void *user_data)
{
    (void)count;
    (void)user_data;

    if (data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN)
        return KaSON_ACTION_ENTER;

    if (data->event != KaSON_STREAM_EVENT_VALUE)
        return KaSON_CALLBACK_CONTINUE;

    if (key_is(key, "name") && data->type == KaSON_TYPE_STRING) {
        printf("name = %.*s\n", (int)(data->end - data->begin + 1),
               data->begin);
    } else if (key_is(key, "age") && data->type == KaSON_TYPE_NUMBER) {
        int age;
        if (kason_value_to_int(data->type, data->begin, data->end, &age) ==
                KaSON_CONVERT_SUCCESS)
            printf("age = %d\n", age);
    }

    return KaSON_CALLBACK_CONTINUE;
}

int main(void)
{
    char json[] = "{\"name\":\"Ada\",\"age\":36}";

    if (kason_parse(json, on_value, NULL) != KaSON_PARSE_RESULT_SUCCESS) {
        fputs("invalid JSON\n", stderr);
        return 1;
    }
    return 0;
}
