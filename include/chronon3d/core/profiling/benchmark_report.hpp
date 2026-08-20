#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace chronon3d {

struct BenchmarkMetrics {
    double time_to_first_frame_ms{};
    double avg_frame_ms{};
    double median_frame_ms{};
    double min_frame_ms{};
    double max_frame_ms{};
    double p50_frame_ms{};
    double p95_frame_ms{};
    double p99_frame_ms{};
    double fps{};
    double fps_steady_state{};
};

struct BenchmarkMemoryMetrics {
    double peak_rss_mb{};
    double peak_framebuffer_bytes{};
    double allocations_per_frame{};
    double bytes_copied_per_frame{};
};

struct BenchmarkQualityMetrics {
    std::string deterministic_hash;
    double ssim{};
};

struct BenchmarkCountersSnapshot {
    uint64_t cache_hits{};
    uint64_t cache_misses{};
    double cache_hit_rate{};
    // TICKET-CACHE-COUNTERS-V1 — domain-scoped cache hit/miss counters so the
    // benchmark can attribute reuse to node/image/font/glyph/gpu asset caches.
    uint64_t node_cache_hits{};
    uint64_t node_cache_misses{};
    uint64_t image_cache_hits{};
    uint64_t image_cache_misses{};
    uint64_t font_cache_hits{};
    uint64_t font_cache_misses{};
    uint64_t glyph_cache_hits{};
    uint64_t glyph_cache_misses{};
    uint64_t gpu_asset_cache_hits{};
    uint64_t gpu_asset_cache_misses{};
    uint64_t nodes_executed{};
    uint64_t pixels_touched{};
    uint64_t blur_pixels{};
    uint64_t images_sampled{};
    uint64_t text_glyphs_rasterized{};
    // TICKET-TEXT-SHAPING-TIMING-V1 — text shaping/bidi telemetry. The call
    // count is the per-frame reshape regression detector: steady state should
    // be ~0 (shaping is prepare-time), a per-frame reshape shows calls ≈ frames.
    uint64_t text_shaping_calls{};
    uint64_t text_shaping_wall_ms{};
    uint64_t text_bidi_wall_ms{};
    uint64_t framebuffer_copies{};
    uint64_t framebuffer_clears{};
    uint64_t full_frame_passes{};
    // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — cumulative atomic raw +
    // dashboard per-frame rates. The raw counters come from
    // CHRONON_COUNTERS_GRAPH (`full_frame_passes` + `full_frame_copies`).
    // The *per_frame rates are derived at snapshot time as
    // `value / graph_executed_frames` (matching the graph_total_ms precedent).
    uint64_t full_frame_copies{};
    // Common performance-gate counters. These are populated by the benchmark
    // producer from the canonical render counters; zero is a valid measured
    // value, while the gate requires the fields to be present in JSON.
    uint64_t bytes_touched{};
    double conversion_ms{};
    uint64_t encoder_copy_bytes{};
    uint64_t nodes_skipped{};
    uint64_t fused_passes{};
    double full_frame_passes_per_frame{};
    double full_frame_copies_per_frame{};
    // TICKET-VIDEO-PIPELINE-BACKPRESSURE-V1 — encoder / conversion / pipe
    // backpressure-aware breakdown. Explicit *_cpu_ms / *_wall_ms /
    // *_wait_ms suffixes distinguish CPU copy vs wall (which may contain
    // poll/back-pressure wait) vs pure back-pressure wait.
    uint64_t encoder_submit_cpu_ms{};
    uint64_t encoder_backpressure_wait_ms{};
    uint64_t encoder_flush_wall_ms{};
    uint64_t mux_finalize_wall_ms{};
    uint64_t pixel_format_convert_wall_ms{};
    uint64_t color_space_convert_wall_ms{};
    uint64_t pipe_write_cpu_ms{};
    uint64_t pipe_write_wall_ms{};
    uint64_t cuda_vulkan_wait_count{};
    uint64_t cuda_vulkan_wait_submit_us{};
    uint64_t cuda_vulkan_signal_count{};
    uint64_t cuda_vulkan_signal_submit_us{};
    uint64_t cuda_composite_frames{};
    uint64_t cuda_composite_wall_us{};
    uint64_t cuda_encode_queue_peak{};
    uint64_t cuda_encode_event_wait_count{};
    uint64_t cuda_encode_event_wait_us{};
};

/// Returns current UTC time formatted as ISO 8601 (e.g. "2026-05-21T12:00:00Z").
std::string current_utc_timestamp_iso();

struct BenchmarkReport {
    std::string schema{"chronon3d.bench.v3"};
    std::string comp_id;
    std::string timestamp_utc{current_utc_timestamp_iso()};
    std::string build_type;
    std::string compiler_info;
    std::string os;
    int width{};
    int height{};
    int frames{};
    int warmup{};
    bool modular_graph{true};
    BenchmarkMetrics metrics{};
    BenchmarkMemoryMetrics memory{};
    BenchmarkQualityMetrics quality{};
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

/// Returns a short OS identifier (e.g. "Linux", "Windows", "macOS").
constexpr const char* detect_os() {
#if defined(__linux__)
    return "Linux";
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
