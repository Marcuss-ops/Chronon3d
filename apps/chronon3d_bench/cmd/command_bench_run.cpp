#include "command_bench_internal.hpp"
#include "../args.hpp"
#include "../../chronon3d_cli/utils/job/cli_render_utils.hpp"

#ifdef CHRONON3D_BUILD_BENCHMARKS
#include <benchmark/benchmark.h>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan_core.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#endif

namespace chronon3d::cli {

#ifdef CHRONON3D_BUILD_BENCHMARKS
using namespace detail;

int command_bench(const CompositionRegistry& registry, const BenchArgs& args) {
    if (args.frames <= 0) {
        spdlog::error("--frames must be greater than zero");
        return 1;
    }
    if (args.warmup < 0) {
        spdlog::error("--warmup must be zero or greater");
        return 1;
    }

    // The typed lane is the only production execution input: read a
    // RenderPlan, compile it once, then benchmark render_program().
    std::ifstream plan_input(args.plan_file);
    if (!plan_input.is_open()) {
        spdlog::error("Failed to open render plan: {}", args.plan_file);
        return 1;
    }
    std::ostringstream plan_contents;
    plan_contents << plan_input.rdbuf();

    render_plan::RenderPlan plan;
    try {
        const auto root = nlohmann::json::parse(plan_contents.str());
        auto decoded = render_plan::decode_render_plan(root);
        if (!decoded) {
            spdlog::error("RenderPlan decode failed at '{}': {}",
                          decoded.error().path, decoded.error().message);
            return 1;
        }
        plan = std::move(decoded).value();
    } catch (const std::exception& error) {
        spdlog::error("RenderPlan JSON parse failed: {}", error.what());
        return 1;
    }

    RenderSettings settings;
    if (plan.temporal_motion_blur) {
        settings.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
        settings.motion_blur.samples =
            static_cast<int>(plan.temporal_motion_blur->samples);
        settings.motion_blur.shutter_angle_deg =
            plan.temporal_motion_blur->shutter_angle;
        settings.motion_blur.shutter_phase_deg = -90.0f;
    }
    if (args.no_dirty_rects) {
        settings.dirty.enabled = false;
        settings.dirty.use_bitmask = false;
        settings.dirty.use_tiles = false;
    }

    const std::filesystem::path assets_root = args.assets_root.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path{args.assets_root};
    auto renderer = create_renderer(
        registry, settings, std::nullopt, assets_root);

    auto* resolver = renderer->execution_context().text.assets;
    if (resolver == nullptr) {
        spdlog::error("Renderer has no asset resolver");
        return 1;
    }
    compiler::CompileOptions compile_options;
    compile_options.fingerprint.render_settings.width = plan.canvas.width;
    compile_options.fingerprint.render_settings.height = plan.canvas.height;
    compile_options.fingerprint.render_settings.motion_blur =
        plan.temporal_motion_blur.has_value();
    auto compiled_result = compiler::compile(plan, *resolver, compile_options);
    if (!compiled_result) {
        spdlog::error("Program compilation failed: {}",
                      compiled_result.error().message);
        return 1;
    }
    auto program = std::make_shared<const compiler::CompiledProgram>(
        std::move(compiled_result).value());
    const std::string label =
        args.label.empty() ? plan.job_id : args.label;

    std::unique_ptr<ScopedSpdlogLevel> quiet_log_guard;
    if (args.quiet) quiet_log_guard = std::make_unique<ScopedSpdlogLevel>(spdlog::level::off);
    if (!args.quiet) {
        spdlog::info("Benchmarking {} (warmup: {}, frames: {})", label, args.warmup, args.frames);
    }

    if (args.warmup_renderer) {
        const auto preparation = runtime::prepare_render(
            renderer.get(), *program,
            runtime::RenderPreparationOptions{
                .warmup_renderer = true,
                .warmup = runtime::RendererWarmupOptions{
                    .width = plan.canvas.width,
                    .height = plan.canvas.height,
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
        renderer->render_program(*program, static_cast<Frame>(i));
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
    context.program = std::move(program);
    context.renderer = std::move(renderer);
    context.frames = args.frames;
    context.warmup = args.warmup;
    context.no_dirty_rects = args.no_dirty_rects;
    context.renderer->session().memory_aggregate().reset();
    g_bench_context = &context;

    benchmark::AddCustomContext("label", label);
    benchmark::AddCustomContext("plan_file", args.plan_file);
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

    const std::string benchmark_name = "chronon3d/render/" + label;
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
        stats_js["comp_id"] = label;
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

    if (!args.node_memory_json_file.empty() && context.renderer) {
        const auto report = context.renderer->session().memory_aggregate().snapshot();
        nlohmann::json memory_js;
        memory_js["schema"] = "chronon3d.stats.v1";
        memory_js["session_id"] = fmt::format("bench-{}", chronon3d::profiling::timestamp_ns());
        memory_js["scene_id"] = label;
        memory_js["frame_count"] = context.frames_rendered.load(std::memory_order_relaxed);
        memory_js["nodes"] = nlohmann::json::array();
        for (const auto& node : report.nodes) {
            memory_js["nodes"].push_back({
                {"node_id", node.node_id},
                {"node_type", node.node_type},
                {"pixels_read", node.pixels_read},
                {"pixels_written", node.pixels_written},
                {"bytes_read", node.bytes_read},
                {"bytes_written", node.bytes_written},
                {"framebuffer_copies", node.framebuffer_copies},
                {"framebuffer_clears", node.framebuffer_clears},
                {"allocations", node.allocations},
                {"allocated_bytes", node.allocated_bytes},
                {"temporary_buffers", node.temporary_buffers},
                {"live_bytes", node.live_bytes},
                {"peak_live_bytes", node.peak_live_bytes},
            });
        }

        std::ofstream memory_out(args.node_memory_json_file);
        if (!memory_out.is_open()) {
            spdlog::error("Failed to open node memory JSON output file: {}", args.node_memory_json_file);
            exit_code = 1;
        } else {
            memory_out << memory_js.dump(2) << '\n';
            memory_out.close();
            if (!memory_out) {
                spdlog::error("Failed while writing node memory JSON output file: {}", args.node_memory_json_file);
                exit_code = 1;
            } else if (!args.quiet) {
                spdlog::info("Node memory JSON written to {} ({} nodes, {} measured frames)",
                             args.node_memory_json_file, report.nodes.size(),
                             context.frames_rendered.load(std::memory_order_relaxed));
            }
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
