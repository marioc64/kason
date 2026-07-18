#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "example_io.h"
#include "kason.h"

typedef struct yaml_output {
    FILE *file;
    int depth;
    int containers[KaSON_MAX_NESTING];
    int failed;
} yaml_output;

static int validate_event(kason_key *key, kason_data *data, int count, void *user_data)
{
    (void)key; (void)count; (void)user_data;
    return data->event == KaSON_STREAM_EVENT_CONTAINER_BEGIN ?
        KaSON_ACTION_SKIP : KaSON_CALLBACK_CONTINUE;
}

static int yaml_indent(yaml_output *out, int depth)
{
    int i;
    for (i = 0; i < depth * 2; ++i) {
        if (fputc(' ', out->file) == EOF) return -1;
    }
    return 0;
}

static int yaml_prefix(yaml_output *out, kason_key *key)
{
    int indent = out->depth > 0 ? out->depth - 1 : 0;
    if (yaml_indent(out, indent) != 0) return -1;
    if (out->depth > 0 && out->containers[out->depth - 1] == KaSON_TYPE_ARRAY) {
        return fputs("- ", out->file) == EOF ? -1 : 0;
    }
    if (key != NULL) {
        size_t length = (size_t)(key->end - key->begin + 1);
        if (fputc('"', out->file) == EOF ||
                (length > 0 && fwrite(key->begin, 1, length, out->file) != length) ||
                fputs("\": ", out->file) == EOF) return -1;
    }
    return 0;
}

static int yaml_value(yaml_output *out, const kason_data *data)
{
    size_t length = (size_t)(data->end - data->begin + 1);
    if (data->type == KaSON_TYPE_STRING && fputc('"', out->file) == EOF) return -1;
    if (length > 0 && fwrite(data->begin, 1, length, out->file) != length) return -1;
    if (data->type == KaSON_TYPE_STRING && fputc('"', out->file) == EOF) return -1;
    return fputc('\n', out->file) == EOF ? -1 : 0;
}

static int emit_yaml(kason_key *key, kason_data *data, int count, void *user_data)
{
    yaml_output *out = (yaml_output *)user_data;
    (void)count;

    if (out->failed) return KaSON_CALLBACK_BREAK;
    if (data->event == KaSON_STREAM_EVENT_CONTAINER_END) {
        --out->depth;
        return KaSON_CALLBACK_CONTINUE;
    }
    if (yaml_prefix(out, key) != 0) {
        out->failed = 1;
        return KaSON_CALLBACK_BREAK;
    }
    if (data->event == KaSON_STREAM_EVENT_VALUE) {
        if (yaml_value(out, data) != 0) out->failed = 1;
        return out->failed ? KaSON_CALLBACK_BREAK : KaSON_CALLBACK_CONTINUE;
    }
    {
        const char *next = data->begin + 1;
        char closing = data->type == KaSON_TYPE_OBJECT ? '}' : ']';
        while (isspace((unsigned char)*next)) ++next;
        if (*next == closing) {
            if (fputs(data->type == KaSON_TYPE_OBJECT ? "{}\n" : "[]\n", out->file) == EOF)
                out->failed = 1;
            return out->failed ? KaSON_ACTION_BREAK : KaSON_ACTION_SKIP;
        }
    }
    if (out->depth > 0 && fputc('\n', out->file) == EOF) {
        out->failed = 1;
        return KaSON_ACTION_BREAK;
    }
    out->containers[out->depth++] = data->type;
    return KaSON_ACTION_ENTER;
}

int main(int argc, char **argv)
{
    char *input;
    FILE *output;
    yaml_output state;
    int result;
    int status = 0;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [input.json [output.yaml]]\n", argv[0]);
        return 2;
    }
    input = example_read_input(argc > 1 ? argv[1] : NULL);
    if (input == NULL) return 1;
    if (kason_parse(input, validate_event, NULL) != KaSON_PARSE_RESULT_SUCCESS) {
        fputs("invalid JSON\n", stderr);
        free(input);
        return 1;
    }
    output = example_open_output(argc > 2 ? argv[2] : NULL);
    if (output == NULL) { free(input); return 1; }
    state.file = output;
    state.depth = 0;
    state.failed = 0;
    result = kason_parse(input, emit_yaml, &state);
    if (result != KaSON_PARSE_RESULT_SUCCESS || state.failed) {
        fputs("output error\n", stderr);
        status = 1;
    }
    if (example_close_output(output, argc > 2 ? argv[2] : NULL) != 0) status = 1;
    free(input);
    return status;
}
