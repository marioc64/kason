#include <stdio.h>
#include <string.h>

#include "kason.h"

static int on_fragment(kason_key *key, kason_stream_data *data,
                       void *user_data)
{
    (void)user_data;

    if (data->event == KaSON_STREAM_EVENT_VALUE) {
        if (key != NULL)
            printf("%.*s = ", (int)(key->end - key->begin + 1), key->begin);
        printf("%.*s\n", (int)(data->end - data->begin + 1), data->begin);
    }
    return KaSON_CALLBACK_CONTINUE;
}

int main(void)
{
    char first[] = "{\"name\":\"A";
    char second[] = "da\",\"age\":36}";
    char scratch[128];
    kason_stream stream;
    int result;

    result = kason_stream_init(&stream, scratch, (int)sizeof(scratch),
                               on_fragment, NULL);
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == 0)
        result = kason_stream_feed(&stream, first, (int)strlen(first));
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == 0)
        result = kason_stream_feed(&stream, second, (int)strlen(second));
    if ((result & KaSON_PARSE_RESULT_MAJOR_MASK) == 0)
        result = kason_stream_finish(&stream);

    if (result != KaSON_PARSE_RESULT_SUCCESS) {
        fputs("invalid or incomplete JSON\n", stderr);
        return 1;
    }
    return 0;
}
