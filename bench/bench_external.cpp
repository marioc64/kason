#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "bench_external.h"
#include "bench_rss.h"
#include "bench_struct.h"

static volatile unsigned long long bench_sink;

static double now_seconds()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    std::chrono::duration<double> elapsed = clock::now() - start;
    return elapsed.count();
}

static std::string make_flat_json()
{
    std::string out;
    out.reserve(900000);
    out.push_back('{');
    for (int i = 0; i < 50000; ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += "\"k";
        out += std::to_string(i);
        out += "\":";
        out += std::to_string(i);
    }
    out.push_back('}');
    return out;
}

static std::string make_nested_json()
{
    std::string out;
    out.reserve(900000);
    out.push_back('{');
    for (int object = 0; object < 256; ++object) {
        if (object > 0) {
            out.push_back(',');
        }
        out += "\"o";
        out += std::to_string(object);
        out += "\":{";
        for (int prop = 0; prop < 256; ++prop) {
            if (prop > 0) {
                out.push_back(',');
            }
            out += "\"p";
            out += std::to_string(prop);
            out += "\":";
            out += std::to_string(object + prop);
        }
        out.push_back('}');
    }
    out.push_back('}');
    return out;
}

static void print_usage(const char *argv0)
{
    std::fprintf(stderr,
                 "usage: %s %s <flat|nested|lookup|struct|struct64|struct64-sparse> <iterations>\n",
                 argv0,
                 benchmark_parser_name());
}

int main(int argc, char **argv)
{
    if (argc != 4 || std::strcmp(argv[1], benchmark_parser_name()) != 0) {
        print_usage(argv[0]);
        return 2;
    }

    int iterations = std::atoi(argv[3]);
    if (iterations <= 0) {
        print_usage(argv[0]);
        return 2;
    }

    std::string json;
    bool lookup_case = std::strcmp(argv[2], "lookup") == 0;
    bool struct_case = std::strcmp(argv[2], "struct") == 0;
    bool struct64_case = std::strcmp(argv[2], "struct64") == 0;
    bool struct64_sparse_case = std::strcmp(argv[2], "struct64-sparse") == 0;
    if (std::strcmp(argv[2], "flat") == 0 || lookup_case) {
        json = make_flat_json();
    } else if (std::strcmp(argv[2], "nested") == 0) {
        json = make_nested_json();
    } else if (struct_case) {
        json = make_struct_json();
    } else if (struct64_case) {
        json = make_struct64_json();
    } else if (struct64_sparse_case) {
        json = make_struct64_sparse_json();
    } else {
        print_usage(argv[0]);
        return 2;
    }

    unsigned long long rss_before = benchmark_rss_bytes();
    void *hold = benchmark_hold(json.data(), json.size(), &bench_sink);
    if (hold == NULL) {
        std::fprintf(stderr, "parse failed for %s/%s\n", argv[1], argv[2]);
        return 1;
    }
    unsigned long long rss_after = benchmark_rss_bytes();
    benchmark_release(hold);

    if (!(lookup_case
              ? benchmark_lookup(json.data(), json.size(), &bench_sink)
              : (struct_case
                    ? benchmark_struct(json.data(), json.size(), &bench_sink)
                    : (struct64_case || struct64_sparse_case
                          ? benchmark_struct64(json.data(), json.size(), &bench_sink)
                          : benchmark_parse(json.data(), json.size(), &bench_sink))))) {
        std::fprintf(stderr, "warmup failed for %s/%s\n", argv[1], argv[2]);
        return 1;
    }

    double start = now_seconds();
    for (int i = 0; i < iterations; ++i) {
        if (!(lookup_case
                  ? benchmark_lookup(json.data(), json.size(), &bench_sink)
                  : (struct_case
                        ? benchmark_struct(json.data(), json.size(), &bench_sink)
                        : (struct64_case || struct64_sparse_case
                              ? benchmark_struct64(json.data(), json.size(), &bench_sink)
                              : benchmark_parse(json.data(), json.size(), &bench_sink))))) {
            std::fprintf(stderr, "timed parse failed for %s/%s\n", argv[1], argv[2]);
            return 1;
        }
    }
    double elapsed = now_seconds() - start;
    double mib = ((double)json.size() * (double)iterations) / (1024.0 * 1024.0);
    double mib_per_second = elapsed > 0.0 ? mib / elapsed : 0.0;
    long long rss_delta = (long long)rss_after - (long long)rss_before;

    std::printf("parser,case,bytes,iterations,seconds,mib_per_second,rss_before,rss_after,rss_delta,sink\n");
    std::printf("%s,%s,%zu,%d,%.6f,%.2f,%llu,%llu,%lld,%llu\n",
                argv[1],
                argv[2],
                json.size(),
                iterations,
                elapsed,
                mib_per_second,
                rss_before,
                rss_after,
                rss_delta,
                (unsigned long long)bench_sink);
    return 0;
}
