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
    if (!report.timestamp_utc.empty()) {
        js["timestamp_utc"] = report.timestamp_utc;
    }
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
    metrics["avg_frame_ms"] = report.metrics.avg_frame_ms;
    metrics["median_frame_ms"] = report.metrics.median_frame_ms;
    metrics["min_frame_ms"] = report.metrics.min_frame_ms;
    metrics["max_frame_ms"] = report.metrics.max_frame_ms;
    metrics["p95_frame_ms"] = report.metrics.p95_frame_ms;
    metrics["fps"] = report.metrics.fps;
    js["metrics"] = metrics;

    nlohmann::json counters;
    counters["cache_hits"] = report.counters.cache_hits;
    counters["cache_misses"] = report.counters.cache_misses;
    counters["cache_hit_rate"] = report.counters.cache_hit_rate;
    counters["nodes_executed"] = report.counters.nodes_executed;
    counters["pixels_touched"] = report.counters.pixels_touched;
    counters["blur_pixels"] = report.counters.blur_pixels;
    counters["images_sampled"] = report.counters.images_sampled;
    counters["text_glyphs_rasterized"] = report.counters.text_glyphs_rasterized;
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
        report.metrics.avg_frame_ms = metrics.value("avg_frame_ms", 0.0);
        report.metrics.median_frame_ms = metrics.value("median_frame_ms", 0.0);
        report.metrics.min_frame_ms = metrics.value("min_frame_ms", 0.0);
        report.metrics.max_frame_ms = metrics.value("max_frame_ms", 0.0);
        report.metrics.p95_frame_ms = metrics.value("p95_frame_ms", 0.0);
        report.metrics.fps = metrics.value("fps", 0.0);
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
