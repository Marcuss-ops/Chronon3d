#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/core/counters.hpp>
#include <chronon3d/core/benchmark_report.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <vector>

namespace chronon3d {
namespace cli {

int command_bench(const CompositionRegistry& registry, const BenchArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }
    if (args.frames <= 0) {
        spdlog::error("--frames must be greater than zero");
        return 1;
    }
    if (args.warmup < 0) {
        spdlog::error("--warmup must be zero or greater");
        return 1;
    }

    auto comp = registry.create(args.comp_id);
    auto renderer = create_renderer(registry, RenderSettings{.use_modular_graph = args.use_modular_graph});

    if (!args.quiet) {
        spdlog::info("Benchmarking {} (warmup: {}, frames: {})", args.comp_id, args.warmup, args.frames);
    }

    // 1. Warmup
    for (int i = 0; i < args.warmup; ++i) {
        const auto frame = static_cast<Frame>(i);
        auto scene = comp.evaluate(frame);
        renderer->render_scene(scene, comp.camera, comp.width(), comp.height());
    }

    // 2. Clear trace and counters before the timed runs
    renderer->trace()->clear();
    renderer->counters()->reset();

    // 3. Timed execution
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < args.frames; ++i) {
        const auto frame = static_cast<Frame>(args.warmup + i);
        auto scene = comp.evaluate(frame);
        renderer->render_scene(scene, comp.camera, comp.width(), comp.height());
    }
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    const double avg_ms = elapsed_ms / static_cast<double>(args.frames);
    const double fps = 1000.0 / avg_ms;

    // 4. Trace event processing (Average, P95, Min, Max, Median, and Category Breakdown)
    const auto& events = renderer->trace()->events();
    std::vector<double> frame_times;
    std::map<std::string, double> category_durations;
    double total_category_time = 0.0;

    for (const auto& ev : events) {
        if (ev.name == "render_scene" || ev.name == "render_frame" || ev.name == "render_scene_2_5d") {
            frame_times.push_back(ev.dur_us / 1000.0);
        } else {
            category_durations[ev.category] += ev.dur_us / 1000.0;
            total_category_time += ev.dur_us / 1000.0;
        }
    }

    // Sort frame times to compute P95, min, max, median
    double p95_ms = avg_ms;
    double min_frame_ms = avg_ms;
    double max_frame_ms = avg_ms;
    double median_frame_ms = avg_ms;

    if (!frame_times.empty()) {
        std::sort(frame_times.begin(), frame_times.end());
        size_t idx = static_cast<size_t>(frame_times.size() * 0.95);
        if (idx >= frame_times.size()) idx = frame_times.size() - 1;
        p95_ms = frame_times[idx];
        min_frame_ms = frame_times.front();
        max_frame_ms = frame_times.back();
        size_t mid = frame_times.size() / 2;
        if (frame_times.size() % 2 == 0) {
            median_frame_ms = (frame_times[mid - 1] + frame_times[mid]) / 2.0;
        } else {
            median_frame_ms = frame_times[mid];
        }
    }

    uint64_t hits = renderer->counters()->cache_hits.load(std::memory_order_relaxed);
    uint64_t misses = renderer->counters()->cache_misses.load(std::memory_order_relaxed);
    double hit_rate = (hits + misses) > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0;

    uint64_t nodes_exec = renderer->counters()->nodes_executed.load(std::memory_order_relaxed);
    uint64_t pixels_touched = renderer->counters()->pixels_touched.load(std::memory_order_relaxed);
    uint64_t blur_pixels = renderer->counters()->blur_pixels.load(std::memory_order_relaxed);
    uint64_t images_sampled = renderer->counters()->images_sampled.load(std::memory_order_relaxed);
    uint64_t glyphs = renderer->counters()->text_glyphs_rasterized.load(std::memory_order_relaxed);

    // 5. Build structured report
    BenchmarkReport report;
    report.comp_id = args.comp_id;
    report.timestamp_utc = current_utc_timestamp_iso();
    report.build_type = detect_build_type();
    report.compiler_info = detect_compiler();
    report.os = detect_os();
    report.width = comp.width();
    report.height = comp.height();
    report.frames = args.frames;
    report.warmup = args.warmup;
    report.modular_graph = args.use_modular_graph;
    report.metrics.avg_frame_ms = avg_ms;
    report.metrics.median_frame_ms = median_frame_ms;
    report.metrics.min_frame_ms = min_frame_ms;
    report.metrics.max_frame_ms = max_frame_ms;
    report.metrics.p95_frame_ms = p95_ms;
    report.metrics.fps = fps;
    report.counters.cache_hits = hits;
    report.counters.cache_misses = misses;
    report.counters.cache_hit_rate = hit_rate;
    report.counters.nodes_executed = nodes_exec;
    report.counters.pixels_touched = pixels_touched;
    report.counters.blur_pixels = blur_pixels;
    report.counters.images_sampled = images_sampled;
    report.counters.text_glyphs_rasterized = glyphs;
    report.categories_ms = category_durations;
    report.frame_times_ms = frame_times;

    // 6. Print terminal report (unless --quiet)
    if (!args.quiet) {
        fmt::print("\n");
        fmt::print("================================================================================\n");
        fmt::print("                    CHRONON3D BENCHMARK REPORT: {}\n", args.comp_id);
        fmt::print("================================================================================\n");
        fmt::print("Frames:           {}\n", args.frames);
        fmt::print("Warmup:           {}\n", args.warmup);
        fmt::print("Average Frame:    {:.3f} ms\n", avg_ms);
        fmt::print("Median Frame:     {:.3f} ms\n", median_frame_ms);
        fmt::print("Min Frame:        {:.3f} ms\n", min_frame_ms);
        fmt::print("Max Frame:        {:.3f} ms\n", max_frame_ms);
        fmt::print("P95 Frame:        {:.3f} ms\n", p95_ms);
        fmt::print("FPS:              {:.2f}\n", fps);
        fmt::print("\n");
        fmt::print("--- Cache Efficiency ---\n");
        fmt::print("Cache Hits:       {}\n", hits);
        fmt::print("Cache Misses:     {}\n", misses);
        fmt::print("Cache Hit Rate:   {:.1f}%\n", hit_rate * 100.0);
        fmt::print("\n");
        fmt::print("--- Resource Metrics ---\n");
        fmt::print("Nodes Executed:   {}\n", nodes_exec);
        fmt::print("Pixels Touched:   {}\n", pixels_touched);
        fmt::print("Blur Pixels:      {}\n", blur_pixels);
        fmt::print("Images Sampled:   {}\n", images_sampled);
        fmt::print("Glyphs Rasterized:{}\n", glyphs);
        fmt::print("\n");
        fmt::print("--- Category Breakdown (Total Duration) ---\n");

        std::vector<std::pair<std::string, double>> sorted_cats(category_durations.begin(), category_durations.end());
        std::sort(sorted_cats.begin(), sorted_cats.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        for (const auto& cat : sorted_cats) {
            double pct = total_category_time > 0.0 ? (cat.second / total_category_time) * 100.0 : 0.0;
            fmt::print("  - {:14}: {:10.3f} ms ({:5.1f}%)\n", cat.first, cat.second, pct);
        }
        fmt::print("================================================================================\n");
        fmt::print("\n");
    } else {
        fmt::print("{}  avg={:.2f}ms  p95={:.2f}ms  fps={:.1f}  cache={:.1f}%\n",
                   args.comp_id, avg_ms, p95_ms, fps, hit_rate * 100.0);
    }

    // 7. Write JSON output if requested
    int exit_code = 0;
    if (!args.json_file.empty()) {
        auto js = to_json(report, args.include_frame_times);
        std::ofstream out(args.json_file);
        if (out.is_open()) {
            out << js.dump(2);
            if (!args.quiet) {
                spdlog::info("Benchmark telemetry saved to {}", args.json_file);
            }
        } else {
            spdlog::error("Failed to open output json file: {}", args.json_file);
            return 1;
        }
    }

    // 8. Compare against baseline if requested
    if (!args.compare_file.empty()) {
        std::ifstream in(args.compare_file);
        if (!in.is_open()) {
            spdlog::error("Failed to open baseline file: {}", args.compare_file);
            return 1;
        }
        nlohmann::json baseline_js;
        try {
            in >> baseline_js;
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse baseline JSON: {}", e.what());
            return 1;
        }
        auto baseline = benchmark_report_from_json(baseline_js);

        double avg_regression = compute_regression_pct(baseline.metrics.avg_frame_ms, report.metrics.avg_frame_ms);
        double p95_regression = compute_regression_pct(baseline.metrics.p95_frame_ms, report.metrics.p95_frame_ms);

        if (!args.quiet) {
            fmt::print("--- Baseline Comparison ---\n");
            fmt::print("Baseline avg:     {:.3f} ms  |  Current avg:     {:.3f} ms  |  Regression: {:+.2f}%\n",
                       baseline.metrics.avg_frame_ms, report.metrics.avg_frame_ms, avg_regression);
            fmt::print("Baseline p95:     {:.3f} ms  |  Current p95:     {:.3f} ms  |  Regression: {:+.2f}%\n",
                       baseline.metrics.p95_frame_ms, report.metrics.p95_frame_ms, p95_regression);
        }

        if (args.fail_if_avg_slower_pct > 0.0 && avg_regression > args.fail_if_avg_slower_pct) {
            spdlog::error("Average frame regression: {:.2f}% (threshold: {:.2f}%)", avg_regression, args.fail_if_avg_slower_pct);
            exit_code = 2;
        }
        if (args.fail_if_p95_slower_pct > 0.0 && p95_regression > args.fail_if_p95_slower_pct) {
            spdlog::error("P95 frame regression: {:.2f}% (threshold: {:.2f}%)", p95_regression, args.fail_if_p95_slower_pct);
            exit_code = 2;
        }
    }

    return exit_code;
}

} // namespace cli
} // namespace chronon3d
