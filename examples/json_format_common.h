#ifndef JSON_FORMAT_COMMON_H
#define JSON_FORMAT_COMMON_H

#include <stdio.h>

int validate_json(char *input);
int write_compact_json(FILE *output, const char *input);
int write_pretty_json(FILE *output, const char *input);

#endif
