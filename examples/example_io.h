#ifndef EXAMPLE_IO_H
#define EXAMPLE_IO_H

#include <stdio.h>

char *example_read_input(const char *path);
FILE *example_open_output(const char *path);
int example_close_output(FILE *output, const char *path);

#endif
