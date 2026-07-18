#ifndef KaSON_BENCH_LOOKUP_H
#define KaSON_BENCH_LOOKUP_H

#include <cstddef>

static const char *const benchmark_lookup_keys[] = {
    "k17",
    "k493",
    "k4096",
    "k9281",
    "k16384",
    "k27183",
    "k40123",
    "k49991"
};

static const unsigned long long benchmark_lookup_values[] = {
    17,
    493,
    4096,
    9281,
    16384,
    27183,
    40123,
    49991
};

static const std::size_t benchmark_lookup_lengths[] = {
    3, 4, 5, 5, 6, 6, 6, 6
};

static constexpr std::size_t benchmark_lookup_count =
    sizeof(benchmark_lookup_keys) / sizeof(benchmark_lookup_keys[0]);
static constexpr unsigned int benchmark_lookup_all_found =
    (1U << benchmark_lookup_count) - 1U;

#endif
