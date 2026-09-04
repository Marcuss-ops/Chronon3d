#include "command_bench_internal.hpp"
#include "../args.hpp"
#include "../../chronon3d_cli/utils/job/cli_render_utils.hpp"

#ifdef CHRONON3D_BUILD_BENCHMARKS
#include <benchmark/benchmark.h>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#endif

namespace chronon3d::cli {

#ifdef CHRONON3D_BUILD_BENCHMARKS
using namespace detail;

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

    auto composition = registry.create(args.comp_id);
    auto compiled_result = chronon3d::compile_composition(composition, CompositionCompileContext{});
    if (!compiled_result) {
        spdlog::error("Composition compilation failed: {}", compiled_result.error().message);
        return 1;
    }
    auto compiled = std::make_shared<const CompiledComposition>(std::move(compiled_result).value());
    RenderSettings settings;
    if (args.no_dirty_rects) {
        settings.dirty.enabled = false;
        settings.dirty.use_bitmask = false;
        settings.dirty.use_tiles = false;
    }
    auto renderer = create_renderer(registry, settings);

    std::unique_ptr<ScopedSpdlogLevel> quiet_log_guard;
    if (args.quiet) quiet_log_guard = std::make_unique<ScopedSpdlogLevel>(spdlog::level::off);
    if (!args.quiet) {
        spdlog::info("Benchmarking {} (warmup: {}, frames: {})", args.comp_id, args.warmup, args.frames);
    }

    if (args.warmup_renderer) {
        const auto preparation = runtime::prepare_render(
            renderer.get(), *compiled,
            runtime::RenderPreparationOptions{
                .warmup_renderer = true,
                .warmup = runtime::RendererWarmupOptions{
                    .width = composition.width(),
                    .height = composition.height(),
                    .framebuffer_count = args.warmup_framebuffers,
                    .preallocate_framebuffers = true,
                    .touch_memory = true,
                    .render_dummy_frame = args.warmup_dummy_frame,
                    .dummy_frame = 0,
                    .quiet = args.quiet,
                },
            });
        if (!preparation.ok()) {
            spdlog::error("Render preparation failed: {}", preparation.diagnostic());
            return 1;
        }
        const auto& warmup = preparation.warmup;
        if (!args.quiet) {
            spdlog::info("Renderer warmup: {} framebuffers, {} bytes, {:.2f} ms",
                         warmup.framebuffers_created, warmup.pool_bytes_after, warmup.elapsed_ms);
        }
        if (renderer->counters()) {
            renderer->counters()->setup_pool_preallocation_wall_ms.fetch_add(
                static_cast<uint64_t>(std::llround(warmup.elapsed_ms)), std::memory_order_relaxed);
        }
    }

    for (int i = 0; i < args.warmup; ++i) {
        renderer->render_compiled(*compiled, static_cast<Frame>(i));
    }

    uint64_t saved_fb_alloc = 0;
    uint64_t saved_fb_reuses = 0;
    uint64_t saved_fb_bytes = 0;
    uint64_t saved_fb_peak = 0;
    if (renderer->counters()) {
        saved_fb_alloc = renderer->counters()->framebuffer_allocations.load(std::memory_order_relaxed);
        saved_fb_reuses = renderer->counters()->framebuffer_reuses.load(std::memory_order_relaxed);
        saved_fb_bytes = renderer->counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
        saved_fb_peak = renderer->counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);
    }
    renderer->counters()->reset();
    if (renderer->counters()) {
        renderer->counters()->framebuffer_allocations.store(saved_fb_alloc, std::memory_order_relaxed);
        renderer->counters()->framebuffer_reuses.store(saved_fb_reuses, std::memory_order_relaxed);
        renderer->counters()->framebuffer_bytes_allocated.store(saved_fb_bytes, std::memory_order_relaxed);
        renderer->counters()->framebuffer_bytes_peak.store(saved_fb_peak, std::memory_order_relaxed);
    }

    chronon3d::telemetry::clear_telemetry_stores();

    BenchRuntimeContext context;
    context.compiled = std::move(compiled);
    context.renderer = std::move(renderer);
    context.frames = args.frames;
    context.warmup = args.warmup;
    context.no_dirty_rects = args.no_dirty_rects;
    g_bench_context = &context;

    benchmark::AddCustomContext("comp_id", args.comp_id);
    benchmark::AddCustomContext("frames", std::to_string(args.frames));
    benchmark::AddCustomContext("warmup", std::to_string(args.warmup));
    benchmark::AddCustomContext("dirty_rects_status", context.no_dirty_rects ? "disabled" : "enabled");
    benchmark::AddCustomContext("warmup_renderer", args.warmup_renderer ? "true" : "false");

    std::optional<std::filesystem::path> temp_json_path;
    std::filesystem::path current_json_path = args.json_file.empty()
        ? std::filesystem::path{} : std::filesystem::path(args.json_file);
    if (current_json_path.empty() || !args.compare_file.empty()) {
        temp_json_path = make_temp_json_path();
        if (!temp_json_path) {
            spdlog::error("Failed to allocate temporary benchmark output path");
            g_bench_context = nullptr;
            return 1;
        }
        if (current_json_path.empty()) current_json_path = *temp_json_path;
    }

    auto benchmark_argv_storage = build_benchmark_argv(args, current_json_path);
    std::vector<char*> benchmark_argv;
    benchmark_argv.reserve(benchmark_argv_storage.size());
    for (auto& arg : benchmark_argv_storage) benchmark_argv.push_back(arg.data());
    int benchmark_argc = static_cast<int>(benchmark_argv.size());
    benchmark::Initialize(&benchmark_argc, benchmark_argv.data());

    benchmark::ConsoleReporter display_reporter(
        args.quiet ? benchmark::ConsoleReporter::OO_None : benchmark::ConsoleReporter::OO_Defaults);
    auto null_stream = make_null_stream();
    if (args.quiet) {
        display_reporter.SetOutputStream(null_stream.get());
        display_reporter.SetErrorStream(null_stream.get());
    }

    std::ofstream json_out(current_json_path);
    if (!json_out.is_open()) {
        spdlog::error("Failed to open benchmark JSON output file: {}", current_json_path.string());
        benchmark::Shutdown();
        g_bench_context = nullptr;
        return 1;
    }
    benchmark::JSONReporter file_reporter;
    file_reporter.SetOutputStream(&json_out);

    const std::string benchmark_name = "chronon3d/render/" + args.comp_id;
    benchmark::RegisterBenchmark(benchmark_name.c_str(), run_render_benchmark)->Unit(benchmark::kMillisecond);
    benchmark::RunSpecifiedBenchmarks(&display_reporter, &file_reporter);
    benchmark::Shutdown();
    json_out.close();

    int exit_code = 0;
    if (!args.quiet) spdlog::info("Benchmark JSON written to {}", current_json_path.string());

    std::vector<BenchmarkSummary> current_rows;
    try {
        current_rows = load_grouped_summaries(current_json_path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to read benchmark JSON: {}", e.what());
        g_bench_context = nullptr;
        return 1;
    }

    if (!args.compare_file.empty()) {
        try {
            const auto baseline_rows = load_grouped_summaries(args.compare_file);
            if (!args.quiet) fmt::print("--- Baseline Comparison ---\n");
            print_comparison(current_rows, baseline_rows,
                             args.fail_if_avg_slower_pct, args.quiet, exit_code);
        } catch (const std::exception& e) {
            spdlog::error("Failed to compare benchmark JSON: {}", e.what());
            g_bench_context = nullptr;
            return 1;
        }
    }

    if (temp_json_path && args.json_file.empty()) {
        std::error_code ec;
        std::filesystem::remove(*temp_json_path, ec);
    }

    if (!args.stats_json_file.empty() && context.renderer && context.renderer->counters()) {
        const auto* rc = context.renderer->counters();
        nlohmann::json stats_js;
        stats_js["comp_id"] = args.comp_id;
        stats_js["passes_before_fusion"] = static_cast<std::uint64_t>(
            rc->pixel_fusion_passes_before.load(std::memory_order_relaxed));
        stats_js["passes_after_fusion"] = static_cast<std::uint64_t>(
            rc->pixel_fusion_passes_after.load(std::memory_order_relaxed));
        stats_js["bytes_saved_by_fusion"] = static_cast<std::uint64_t>(
            rc->pixel_fusion_bytes_saved.load(std::memory_order_relaxed));
        std::ofstream stats_out(args.stats_json_file);
        if (stats_out.is_open()) {
            stats_out << stats_js.dump(2) << '\n';
            stats_out.close();
            if (!args.quiet) {
                spdlog::info("F3.1 fusion stats JSON written to {} (passes_before={}, passes_after={}, bytes_saved={})",
                             args.stats_json_file,
                             stats_js["passes_before_fusion"].get<std::uint64_t>(),
                             stats_js["passes_after_fusion"].get<std::uint64_t>(),
                             stats_js["bytes_saved_by_fusion"].get<std::uint64_t>());
            }
        } else {
            spdlog::error("Failed to open F3.1 --stats-json output file: {}", args.stats_json_file);
        }
    }

    g_bench_context = nullptr;
    return exit_code;
}

#else
int command_bench(const CompositionRegistry&, const BenchArgs&) {
    spdlog::error("Benchmarks not available: built without Google Benchmark support");
    return 1;
}
#endif

} // namespace chronon3d::cli
