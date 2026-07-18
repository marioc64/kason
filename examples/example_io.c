#include <stdio.h>
#include <stdlib.h>

#include "example_io.h"

char *example_read_input(const char *path)
{
    FILE *input = stdin;
    char *buffer;
    size_t capacity = 4096;
    size_t length = 0;

    if (path != NULL) {
        input = fopen(path, "rb");
        if (input == NULL) {
            perror(path);
            return NULL;
        }
    }

    buffer = (char *)malloc(capacity + 1);
    if (buffer == NULL) {
        fprintf(stderr, "out of memory\n");
        if (input != stdin) {
            fclose(input);
        }
        return NULL;
    }

    for (;;) {
        size_t available = capacity - length;
        size_t received = fread(buffer + length, 1, available, input);

        length += received;
        if (received < available) {
            if (ferror(input)) {
                perror(path != NULL ? path : "stdin");
                free(buffer);
                if (input != stdin) {
                    fclose(input);
                }
                return NULL;
            }
            break;
        }
        if (capacity > ((size_t)-1 - 1) / 2) {
            fprintf(stderr, "input is too large\n");
            free(buffer);
            if (input != stdin) {
                fclose(input);
            }
            return NULL;
        }
        capacity *= 2;
        {
            char *larger = (char *)realloc(buffer, capacity + 1);
            if (larger == NULL) {
                fprintf(stderr, "out of memory\n");
                free(buffer);
                if (input != stdin) {
                    fclose(input);
                }
                return NULL;
            }
            buffer = larger;
        }
    }

    if (input != stdin && fclose(input) != 0) {
        perror(path);
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}

FILE *example_open_output(const char *path)
{
    FILE *output;

    if (path == NULL) {
        return stdout;
    }
    output = fopen(path, "wb");
    if (output == NULL) {
        perror(path);
    }
    return output;
}

int example_close_output(FILE *output, const char *path)
{
    if (output == stdout) {
        if (fflush(output) != 0) {
            perror("stdout");
            return -1;
        }
        return 0;
    }
    if (fclose(output) != 0) {
        perror(path);
        return -1;
    }
    return 0;
}
