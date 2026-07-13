#include <chronon3d/core/profiling/benchmark_report.hpp>

#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace chronon3d {

std::string current_utc_timestamp_iso() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

nlohmann::json to_json(const BenchmarkReport& report, bool include_frame_times) {
    nlohmann::json js;
    js["schema"] = report.schema;
    js["comp_id"] = report.comp_id;
    js["timestamp_utc"] = report.timestamp_utc;
    js["build_type"] = report.build_type;
    js["compiler_info"] = report.compiler_info;
    js["os"] = report.os;

    nlohmann::json render;
    render["width"] = report.width;
    render["height"] = report.height;
    render["frames"] = report.frames;
    render["warmup"] = report.warmup;
    render["modular_graph"] = report.modular_graph;
    js["render"] = render;

    nlohmann::json metrics;
    metrics["time_to_first_frame_ms"] = report.metrics.time_to_first_frame_ms;
    metrics["avg_frame_ms"] = report.metrics.avg_frame_ms;
    metrics["median_frame_ms"] = report.metrics.median_frame_ms;
    metrics["min_frame_ms"] = report.metrics.min_frame_ms;
    metrics["max_frame_ms"] = report.metrics.max_frame_ms;
    metrics["p50_frame_ms"] = report.metrics.p50_frame_ms;
    metrics["p95_frame_ms"] = report.metrics.p95_frame_ms;
    metrics["p99_frame_ms"] = report.metrics.p99_frame_ms;
    metrics["fps"] = report.metrics.fps;
    metrics["fps_steady_state"] = report.metrics.fps_steady_state;
    js["metrics"] = metrics;

    nlohmann::json memory;
    memory["peak_rss_mb"] = report.memory.peak_rss_mb;
    memory["peak_framebuffer_bytes"] = report.memory.peak_framebuffer_bytes;
    memory["allocations_per_frame"] = report.memory.allocations_per_frame;
    memory["bytes_copied_per_frame"] = report.memory.bytes_copied_per_frame;
    js["memory"] = memory;

    nlohmann::json quality;
    quality["deterministic_hash"] = report.quality.deterministic_hash;
    quality["ssim"] = report.quality.ssim;
    js["quality"] = quality;

    nlohmann::json counters;
    counters["cache_hits"] = report.counters.cache_hits;
    counters["cache_misses"] = report.counters.cache_misses;
    counters["cache_hit_rate"] = report.counters.cache_hit_rate;
    counters["nodes_executed"] = report.counters.nodes_executed;
    counters["pixels_touched"] = report.counters.pixels_touched;
    counters["blur_pixels"] = report.counters.blur_pixels;
    counters["images_sampled"] = report.counters.images_sampled;
    counters["text_glyphs_rasterized"] = report.counters.text_glyphs_rasterized;
    counters["framebuffer_copies"] = report.counters.framebuffer_copies;
    counters["framebuffer_clears"] = report.counters.framebuffer_clears;
    counters["full_frame_passes"] = report.counters.full_frame_passes;
    js["counters"] = counters;

    nlohmann::json cats = nlohmann::json::object();
    for (const auto& [k, v] : report.categories_ms) {
        cats[k] = v;
    }
    js["categories_ms"] = cats;

    if (!report.node_durations_ms.empty()) {
        nlohmann::json nodes = nlohmann::json::object();
        for (const auto& [k, v] : report.node_durations_ms) {
            nodes[k] = v;
        }
        js["node_durations_ms"] = nodes;
    }

    if (include_frame_times && !report.frame_times_ms.empty()) {
        js["frame_times_ms"] = report.frame_times_ms;
    }

    return js;
}

BenchmarkReport benchmark_report_from_json(const nlohmann::json& js) {
    BenchmarkReport report;
    report.schema = js.value("schema", "chronon3d.bench.v2");
    report.comp_id = js.value("comp_id", "");
    report.timestamp_utc = js.value("timestamp_utc", "");
    report.build_type = js.value("build_type", "");
    report.compiler_info = js.value("compiler_info", "");
    report.os = js.value("os", "");

    if (js.contains("render") && js["render"].is_object()) {
        const auto& render = js["render"];
        report.width = render.value("width", 0);
        report.height = render.value("height", 0);
        report.frames = render.value("frames", 0);
        report.warmup = render.value("warmup", 0);
        report.modular_graph = render.value("modular_graph", false);
    }

    if (js.contains("metrics") && js["metrics"].is_object()) {
        const auto& metrics = js["metrics"];
        report.metrics.time_to_first_frame_ms = metrics.value("time_to_first_frame_ms", 0.0);
        report.metrics.avg_frame_ms = metrics.value("avg_frame_ms", 0.0);
        report.metrics.median_frame_ms = metrics.value("median_frame_ms", 0.0);
        report.metrics.min_frame_ms = metrics.value("min_frame_ms", 0.0);
        report.metrics.max_frame_ms = metrics.value("max_frame_ms", 0.0);
        report.metrics.p50_frame_ms = metrics.value("p50_frame_ms", 0.0);
        report.metrics.p95_frame_ms = metrics.value("p95_frame_ms", 0.0);
        report.metrics.p99_frame_ms = metrics.value("p99_frame_ms", 0.0);
        report.metrics.fps = metrics.value("fps", 0.0);
        report.metrics.fps_steady_state = metrics.value("fps_steady_state", 0.0);
    }

    if (js.contains("memory") && js["memory"].is_object()) {
        const auto& memory = js["memory"];
        report.memory.peak_rss_mb = memory.value("peak_rss_mb", 0.0);
        report.memory.peak_framebuffer_bytes = memory.value("peak_framebuffer_bytes", 0.0);
        report.memory.allocations_per_frame = memory.value("allocations_per_frame", 0.0);
        report.memory.bytes_copied_per_frame = memory.value("bytes_copied_per_frame", 0.0);
    }

    if (js.contains("quality") && js["quality"].is_object()) {
        const auto& quality = js["quality"];
        report.quality.deterministic_hash = quality.value("deterministic_hash", std::string{});
        report.quality.ssim = quality.value("ssim", 0.0);
    }

    if (js.contains("counters") && js["counters"].is_object()) {
        const auto& counters = js["counters"];
        report.counters.cache_hits = counters.value("cache_hits", uint64_t{0});
        report.counters.cache_misses = counters.value("cache_misses", uint64_t{0});
        report.counters.cache_hit_rate = counters.value("cache_hit_rate", 0.0);
        report.counters.nodes_executed = counters.value("nodes_executed", uint64_t{0});
        report.counters.pixels_touched = counters.value("pixels_touched", uint64_t{0});
        report.counters.blur_pixels = counters.value("blur_pixels", uint64_t{0});
        report.counters.images_sampled = counters.value("images_sampled", uint64_t{0});
        report.counters.text_glyphs_rasterized = counters.value("text_glyphs_rasterized", uint64_t{0});
        report.counters.framebuffer_copies = counters.value("framebuffer_copies", uint64_t{0});
        report.counters.framebuffer_clears = counters.value("framebuffer_clears", uint64_t{0});
        report.counters.full_frame_passes = counters.value("full_frame_passes", uint64_t{0});
    }

    if (js.contains("categories_ms") && js["categories_ms"].is_object()) {
        for (const auto& [k, v] : js["categories_ms"].items()) {
            if (v.is_number()) {
                report.categories_ms[k] = v.get<double>();
            }
        }
    }

    if (js.contains("node_durations_ms") && js["node_durations_ms"].is_object()) {
        for (const auto& [k, v] : js["node_durations_ms"].items()) {
            if (v.is_number()) {
                report.node_durations_ms[k] = v.get<double>();
            }
        }
    }

    if (js.contains("frame_times_ms") && js["frame_times_ms"].is_array()) {
        report.frame_times_ms = js["frame_times_ms"].get<std::vector<double>>();
    }

    return report;
}

double compute_regression_pct(double baseline, double current) {
    if (baseline == 0.0) return 0.0;
    return ((current - baseline) / baseline) * 100.0;
}

} // namespace chronon3d
