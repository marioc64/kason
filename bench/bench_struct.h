#ifndef KaSON_BENCH_STRUCT_H
#define KaSON_BENCH_STRUCT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

struct BenchmarkRecord {
    char name[64];
    std::uint32_t id;
    int offset;
    double ratio;
};

inline constexpr std::size_t benchmark_large_field_count = 64;

struct BenchmarkLargeRecord {
    std::uint32_t values[benchmark_large_field_count];
};

inline constexpr std::uint32_t benchmark_large_value_base = UINT32_C(100000);

enum {
    benchmark_struct_found_name = 1U << 0,
    benchmark_struct_found_id = 1U << 1,
    benchmark_struct_found_offset = 1U << 2,
    benchmark_struct_found_ratio = 1U << 3,
    benchmark_struct_found_all = (1U << 4) - 1U
};

inline constexpr char benchmark_record_name_key[] = "name";
inline constexpr char benchmark_record_id_key[] = "id";
inline constexpr char benchmark_record_offset_key[] = "offset";
inline constexpr char benchmark_record_ratio_key[] = "ratio";
inline constexpr char benchmark_record_name[] = "Alice Example";
inline constexpr std::uint32_t benchmark_record_id = UINT32_C(4000000000);
inline constexpr int benchmark_record_offset = -123456789;
inline constexpr double benchmark_record_ratio = 12345.625;

inline std::string make_struct_json()
{
    std::string out;
    out.reserve(900000);
    out.push_back('{');
    for (int i = 0; i < 50000; ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        if (i == 17) {
            out += "\"name\":\"";
            out += benchmark_record_name;
            out.push_back('"');
        } else if (i == 493) {
            out += "\"id\":";
            out += std::to_string(benchmark_record_id);
        } else if (i == 4096) {
            out += "\"offset\":";
            out += std::to_string(benchmark_record_offset);
        } else if (i == 27183) {
            out += "\"ratio\":";
            out += "12345.625";
        } else {
            out += "\"k";
            out += std::to_string(i);
            out += "\":";
            out += std::to_string(i);
        }
    }
    out.push_back('}');
    return out;
}

inline std::string make_struct64_json()
{
    std::string out;
    out.reserve(1400);
    out.push_back('{');
    for (std::size_t i = 0; i < benchmark_large_field_count; ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += "\"field";
        out += std::to_string(i);
        out += "\":";
        out += std::to_string(benchmark_large_value_base +
                              (std::uint32_t)i);
    }
    out.push_back('}');
    return out;
}

inline std::string make_struct64_sparse_json()
{
    constexpr int property_count = 50000;
    std::string out;
    std::size_t field = 0;
    out.reserve(900000);
    out.push_back('{');
    for (int i = 0; i < property_count; ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        int field_position = (int)((field + 1) * property_count /
                                   (benchmark_large_field_count + 1));
        if (field < benchmark_large_field_count && i == field_position) {
            out += "\"field";
            out += std::to_string(field);
            out += "\":";
            out += std::to_string(benchmark_large_value_base +
                                  (std::uint32_t)field);
            ++field;
        } else {
            out += "\"k";
            out += std::to_string(i);
            out += "\":";
            out += std::to_string(i);
        }
    }
    out.push_back('}');
    return out;
}

inline bool benchmark_record_valid(const BenchmarkRecord &record)
{
    return std::strcmp(record.name, benchmark_record_name) == 0 &&
        record.id == benchmark_record_id &&
        record.offset == benchmark_record_offset &&
        record.ratio == benchmark_record_ratio;
}

inline bool benchmark_record_set_name(BenchmarkRecord &record,
                                      const char *name,
                                      std::size_t length)
{
    if (name == nullptr || length >= sizeof(record.name)) {
        return false;
    }
    std::memcpy(record.name, name, length);
    record.name[length] = '\0';
    return true;
}

inline unsigned long long benchmark_record_checksum(const BenchmarkRecord &record)
{
    unsigned long long checksum = record.id;
    for (const char *ptr = record.name; *ptr != '\0'; ++ptr) {
        checksum = checksum * 33U + (unsigned char)*ptr;
    }
    checksum += (std::uint32_t)record.offset;
    checksum += (unsigned long long)(record.ratio * 1000.0);
    return checksum;
}

inline bool benchmark_large_record_valid(const BenchmarkLargeRecord &record)
{
    for (std::size_t i = 0; i < benchmark_large_field_count; ++i) {
        if (record.values[i] != benchmark_large_value_base +
                (std::uint32_t)i) {
            return false;
        }
    }
    return true;
}

inline unsigned long long benchmark_large_record_checksum(
    const BenchmarkLargeRecord &record)
{
    unsigned long long checksum = 0;
    for (std::size_t i = 0; i < benchmark_large_field_count; ++i) {
        checksum = checksum * 33U + record.values[i];
    }
    return checksum;
}

#endif
