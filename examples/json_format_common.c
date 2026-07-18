#include <ctype.h>
#include <stdio.h>

#include "json_format_common.h"
#include "kason.h"

static int validate_event(kason_key *key, kason_data *data, int count, void *user_data)
{
    (void)key; (void)count; (void)user_data;
    return data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN ?
        KaSON_ACTION_SKIP : KaSON_CALLBACK_CONTINUE;
}

int validate_json(char *input)
{
    return kason_parse(input, validate_event, NULL) == KaSON_PARSE_RESULT_SUCCESS;
}

int write_compact_json(FILE *output, const char *input)
{
    int in_string = 0, escaped = 0;
    while (*input != '\0') {
        unsigned char ch = (unsigned char)*input++;
        if (!in_string && isspace(ch)) continue;
        if (fputc(ch, output) == EOF) return -1;
        if (in_string) {
            if (escaped) escaped = 0;
            else if (ch == '\\') escaped = 1;
            else if (ch == '"') in_string = 0;
        } else if (ch == '"') in_string = 1;
    }
    return fputc('\n', output) == EOF ? -1 : 0;
}

static int put_indent(FILE *output, int depth)
{
    int i;
    for (i = 0; i < depth * 2; ++i)
        if (fputc(' ', output) == EOF) return -1;
    return 0;
}

int write_pretty_json(FILE *output, const char *input)
{
    int depth = 0, container_depth = 0;
    int expanded[KaSON_MAX_NESTING];
    int in_string = 0, escaped = 0;
    while (*input != '\0') {
        unsigned char ch = (unsigned char)*input++;
        if (in_string) {
            if (fputc(ch, output) == EOF) return -1;
            if (escaped) escaped = 0;
            else if (ch == '\\') escaped = 1;
            else if (ch == '"') in_string = 0;
            continue;
        }
        if (isspace(ch)) continue;
        if (ch == '"') {
            in_string = 1;
            if (fputc(ch, output) == EOF) return -1;
        } else if (ch == '{' || ch == '[') {
            const char *next = input;
            while (isspace((unsigned char)*next)) ++next;
            if (fputc(ch, output) == EOF) return -1;
            expanded[container_depth] = ((ch == '{' && *next != '}') ||
                                         (ch == '[' && *next != ']'));
            if (expanded[container_depth]) {
                ++depth;
                if (fputc('\n', output) == EOF || put_indent(output, depth) != 0) return -1;
            }
            ++container_depth;
        } else if (ch == '}' || ch == ']') {
            --container_depth;
            if (expanded[container_depth]) {
                --depth;
                if (fputc('\n', output) == EOF || put_indent(output, depth) != 0) return -1;
            }
            if (fputc(ch, output) == EOF) return -1;
        } else if (ch == ',') {
            if (fputs(",\n", output) == EOF || put_indent(output, depth) != 0) return -1;
        } else if (ch == ':') {
            if (fputs(": ", output) == EOF) return -1;
        } else if (fputc(ch, output) == EOF) return -1;
    }
    return fputc('\n', output) == EOF ? -1 : 0;
}
