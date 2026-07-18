#include <stdio.h>
#include <stdlib.h>

#include "example_io.h"
#include "json_format_common.h"

int main(int argc, char **argv)
{
    char *input;
    FILE *output;
    int status = 0;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [input.json [output.json]]\n", argv[0]);
        return 2;
    }
    input = example_read_input(argc > 1 ? argv[1] : NULL);
    if (input == NULL) return 1;
    if (!validate_json(input)) {
        fputs("invalid JSON\n", stderr);
        free(input);
        return 1;
    }
    output = example_open_output(argc > 2 ? argv[2] : NULL);
    if (output == NULL) { free(input); return 1; }
    if (write_compact_json(output, input) != 0 ||
            example_close_output(output, argc > 2 ? argv[2] : NULL) != 0) status = 1;
    free(input);
    return status;
}
