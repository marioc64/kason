#ifndef KaSON_BENCH_RSS_H
#define KaSON_BENCH_RSS_H

#include <cstdio>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

static inline unsigned long long benchmark_rss_bytes()
{
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;

    if (task_info(mach_task_self(),
                  MACH_TASK_BASIC_INFO,
                  (task_info_t)&info,
                  &count) != KERN_SUCCESS) {
        return 0;
    }
    return (unsigned long long)info.resident_size;
#elif defined(__linux__)
    std::FILE *statm = std::fopen("/proc/self/statm", "r");
    unsigned long long total_pages;
    unsigned long long resident_pages;

    if (statm == NULL) {
        return 0;
    }
    int fields = std::fscanf(statm, "%llu %llu", &total_pages, &resident_pages);
    std::fclose(statm);
    long page_size = sysconf(_SC_PAGESIZE);
    if (fields != 2 || page_size <= 0) {
        return 0;
    }
    return resident_pages * (unsigned long long)page_size;
#else
    return 0;
#endif
}

#endif
