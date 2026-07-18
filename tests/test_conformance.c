#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kason.h"

static int accept_value(kason_key *key, kason_data *data, int count,
                        void *user_data)
{
    (void)key;
    (void)data;
    (void)count;
    (void)user_data;
    return KaSON_CALLBACK_CONTINUE;
}

static char *read_file(const char *path)
{
    FILE *file;
    char *buffer;
    long size;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    if (size > 0 && fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, char *argv[])
{
    char *input;
    int expect_valid;
    int result;
    int is_valid;

    if (argc != 3 ||
            (strcmp(argv[1], "valid") != 0 &&
             strcmp(argv[1], "invalid") != 0)) {
        fprintf(stderr, "usage: %s valid|invalid FILE\n", argv[0]);
        return 2;
    }

    expect_valid = strcmp(argv[1], "valid") == 0;
    input = read_file(argv[2]);
    if (input == NULL) {
        fprintf(stderr, "unable to read %s\n", argv[2]);
        return 2;
    }

    result = kason_parse(input, accept_value, NULL);
    is_valid = result == KaSON_PARSE_RESULT_SUCCESS;
    free(input);

    if (is_valid != expect_valid) {
        fprintf(stderr, "%s was %s, expected %s (result 0x%04x)\n",
                argv[2], is_valid ? "valid" : "invalid",
                expect_valid ? "valid" : "invalid", result);
        return 1;
    }
    return 0;
}
