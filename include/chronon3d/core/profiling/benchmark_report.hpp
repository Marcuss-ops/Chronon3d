#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace chronon3d {

struct BenchmarkMetrics {
    double avg_frame_ms{};
    double median_frame_ms{};
    double min_frame_ms{};
    double max_frame_ms{};
    double p95_frame_ms{};
    double fps{};
};

struct BenchmarkCountersSnapshot {
    uint64_t cache_hits{};
    uint64_t cache_misses{};
    double cache_hit_rate{};
    uint64_t nodes_executed{};
    uint64_t pixels_touched{};
    uint64_t blur_pixels{};
    uint64_t images_sampled{};
    uint64_t text_glyphs_rasterized{};
};

struct BenchmarkReport {
    std::string schema{"chronon3d.bench.v2"};
    std::string comp_id;
    std::string timestamp_utc;
    std::string build_type;
    std::string compiler_info;
    std::string os;
    int width{};
    int height{};
    int frames{};
    int warmup{};
    bool modular_graph{};
    BenchmarkMetrics metrics{};
    BenchmarkCountersSnapshot counters{};
    std::map<std::string, double> categories_ms;
    std::map<std::string, double> node_durations_ms;
    std::vector<double> frame_times_ms;
};

nlohmann::json to_json(const BenchmarkReport& report, bool include_frame_times = false);
BenchmarkReport benchmark_report_from_json(const nlohmann::json& js);

/// Compute regression percentage: ((current - baseline) / baseline) * 100.0
/// Returns 0.0 if baseline value is zero.
double compute_regression_pct(double baseline, double current);

/// Returns current UTC time formatted as ISO 8601 (e.g. "2026-05-21T12:00:00Z").
std::string current_utc_timestamp_iso();

/// Returns a short OS identifier (e.g. "Linux", "Windows", "macOS").
constexpr const char* detect_os() {
#if defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown";
#endif
}

/// Returns compiler identification string.
constexpr const char* detect_compiler() {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "Unknown";
#endif
}

/// Returns build type string ("Release" or "Debug") based on NDEBUG.
constexpr const char* detect_build_type() {
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

} // namespace chronon3d
