#ifndef KaSON_BENCH_EXTERNAL_H
#define KaSON_BENCH_EXTERNAL_H

#include <stddef.h>

const char *benchmark_parser_name(void);
void *benchmark_hold(const char *json,
                     size_t length,
                     volatile unsigned long long *sink);
void benchmark_release(void *hold);
bool benchmark_parse(const char *json,
                     size_t length,
                     volatile unsigned long long *sink);
bool benchmark_lookup(const char *json,
                      size_t length,
                      volatile unsigned long long *sink);
bool benchmark_struct(const char *json,
                      size_t length,
                      volatile unsigned long long *sink);
bool benchmark_struct64(const char *json,
                        size_t length,
                        volatile unsigned long long *sink);

#endif
