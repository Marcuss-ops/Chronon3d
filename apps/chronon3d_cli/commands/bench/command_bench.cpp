#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <benchmark/benchmark.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/benchmark_report.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

namespace {

class NullBuffer final : public std::streambuf {
protected:
    int overflow(int c) override { return traits_type::not_eof(c); }
};

class NullStream final : public std::ostream {
public:
    NullStream() : std::ostream(&m_buffer) {}

private:
    NullBuffer m_buffer;
};

class ScopedSpdlogLevel final {
public:
    explicit ScopedSpdlogLevel(spdlog::level::level_enum level)
        : m_previous(spdlog::get_level()) {
        spdlog::set_level(level);
    }

    ~ScopedSpdlogLevel() {
        spdlog::set_level(m_previous);
    }

private:
    spdlog::level::level_enum m_previous;
};

struct BenchRuntimeContext {
    std::optional<Composition> composition;
    std::shared_ptr<SoftwareRenderer> renderer;
    int frames{0};
    int warmup{0};
    bool dirty_rects{false};
};

thread_local BenchRuntimeContext* g_bench_context = nullptr;

struct BenchmarkSummary {
    std::string name;
    double real_time_ms{0.0};
    double cpu_time_ms{0.0};
    int64_t iterations{0};
};

std::optional<std::filesystem::path> make_temp_json_path() {
    try {
        auto dir = std::filesystem::temp_directory_path();
        auto stamp = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        return dir / ("chronon3d_bench_" + stamp + ".json");
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<BenchmarkSummary> load_benchmark_summaries(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open benchmark JSON: " + path.string());
    }

    nlohmann::json js;
    in >> js;

    std::vector<BenchmarkSummary> summaries;
    if (!js.contains("benchmarks") || !js["benchmarks"].is_array()) {
        return summaries;
    }

    for (const auto& entry : js["benchmarks"]) {
        if (!entry.is_object()) {
            continue;
        }
        if (entry.value("run_type", "") != "iteration") {
            continue;
        }

        BenchmarkSummary summary;
        summary.name = entry.value("name", "");
        summary.real_time_ms = entry.value("real_time", 0.0);
        summary.cpu_time_ms = entry.value("cpu_time", 0.0);
        summary.iterations = entry.value("iterations", int64_t{0});
        summaries.push_back(std::move(summary));
    }

    return summaries;
}

std::vector<BenchmarkSummary> load_grouped_summaries(const std::filesystem::path& path) {
    auto rows = load_benchmark_summaries(path);
    std::vector<BenchmarkSummary> grouped;
    if (rows.empty()) {
        return grouped;
    }

    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    for (size_t i = 0; i < rows.size();) {
        size_t j = i;
        BenchmarkSummary summary;
        summary.name = rows[i].name;
        double real_total = 0.0;
        double cpu_total = 0.0;
        int64_t iterations_total = 0;
        size_t count = 0;
        while (j < rows.size() && rows[j].name == rows[i].name) {
            real_total += rows[j].real_time_ms;
            cpu_total += rows[j].cpu_time_ms;
            iterations_total += rows[j].iterations;
            ++count;
            ++j;
        }
        summary.real_time_ms = real_total / static_cast<double>(count);
        summary.cpu_time_ms = cpu_total / static_cast<double>(count);
        summary.iterations = iterations_total;
        grouped.push_back(std::move(summary));
        i = j;
    }

    return grouped;
}

void run_render_benchmark(benchmark::State& state) {
    auto& ctx = *g_bench_context;
    const int width = ctx.composition->width();
    const int height = ctx.composition->height();
    auto* counters = ctx.renderer->counters();

    for (auto _ : state) {
        for (int i = 0; i < ctx.frames; ++i) {
            const auto frame = static_cast<Frame>(ctx.warmup + i);
            auto scene = ctx.composition->evaluate(frame);
            ctx.renderer->render_scene(scene, ctx.composition->camera, width, height);
            benchmark::DoNotOptimize(scene);
        }
        benchmark::DoNotOptimize(counters);
        benchmark::ClobberMemory();
    }

    const double iterations = static_cast<double>(state.iterations());
    const double frames = iterations * static_cast<double>(ctx.frames);

    state.SetItemsProcessed(static_cast<int64_t>(frames));
    state.counters["cache_hits"] = static_cast<double>(counters->cache_hits.load(std::memory_order_relaxed));
    state.counters["cache_misses"] = static_cast<double>(counters->cache_misses.load(std::memory_order_relaxed));
    state.counters["nodes_executed"] = static_cast<double>(counters->nodes_executed.load(std::memory_order_relaxed));
    state.counters["pixels_touched"] = static_cast<double>(counters->pixels_touched.load(std::memory_order_relaxed));
    state.counters["blur_pixels"] = static_cast<double>(counters->blur_pixels.load(std::memory_order_relaxed));
    state.counters["images_sampled"] = static_cast<double>(counters->images_sampled.load(std::memory_order_relaxed));
    state.counters["text_glyphs_rasterized"] = static_cast<double>(counters->text_glyphs_rasterized.load(std::memory_order_relaxed));
    state.counters["cache_hit_rate"] = (counters->cache_hits.load(std::memory_order_relaxed) + counters->cache_misses.load(std::memory_order_relaxed)) > 0
        ? static_cast<double>(counters->cache_hits.load(std::memory_order_relaxed)) /
              static_cast<double>(counters->cache_hits.load(std::memory_order_relaxed) + counters->cache_misses.load(std::memory_order_relaxed))
        : 0.0;
}

std::vector<std::string> build_benchmark_argv(const BenchArgs& args, const std::filesystem::path& json_out_path) {
    std::vector<std::string> argv;
    argv.emplace_back("chronon3d_bench");
    argv.emplace_back("--benchmark_min_time=0.1s");
    argv.emplace_back("--benchmark_repetitions=1");
    argv.emplace_back("--benchmark_report_aggregates_only=false");
    argv.emplace_back("--benchmark_display_aggregates_only=false");
    argv.emplace_back("--benchmark_out=" + json_out_path.string());
    argv.emplace_back("--benchmark_out_format=json");

    if (args.quiet) {
        argv.emplace_back("--benchmark_color=false");
    }

    return argv;
}

void print_comparison(const std::vector<BenchmarkSummary>& current,
                      const std::vector<BenchmarkSummary>& baseline,
                      double fail_if_avg_slower_pct,
                      bool quiet,
                      int& exit_code) {
    bool any_match = false;
    for (const auto& cur : current) {
        auto it = std::find_if(baseline.begin(), baseline.end(), [&](const auto& b) {
            return b.name == cur.name;
        });
        if (it == baseline.end()) {
            continue;
        }

        any_match = true;
        const double baseline_ms = it->real_time_ms;
        const double current_ms = cur.real_time_ms;
        const double regression_pct = compute_regression_pct(baseline_ms, current_ms);

        if (!quiet) {
            fmt::print("  - {:32}: baseline={:10.3f} ms  current={:10.3f} ms  delta={:+7.2f}%\n",
                       cur.name, baseline_ms, current_ms, regression_pct);
        }

        if (fail_if_avg_slower_pct > 0.0 && regression_pct > fail_if_avg_slower_pct) {
            spdlog::error("Benchmark regression for {}: {:.2f}% (threshold: {:.2f}%)",
                          cur.name, regression_pct, fail_if_avg_slower_pct);
            exit_code = 2;
        }
    }

    if (!any_match && !quiet) {
        spdlog::warn("No benchmark names matched between current run and baseline");
    }
}

} // namespace

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
    RenderSettings settings;
    settings.use_modular_graph = args.use_modular_graph;
    settings.dirty_rects = args.dirty_rects;
    auto renderer = create_renderer(registry, settings);

    std::unique_ptr<ScopedSpdlogLevel> quiet_log_guard;
    if (args.quiet) {
        quiet_log_guard = std::make_unique<ScopedSpdlogLevel>(spdlog::level::off);
    }

    if (!args.quiet) {
        spdlog::info("Benchmarking {} (warmup: {}, frames: {})", args.comp_id, args.warmup, args.frames);
    }

    if (args.warmup_renderer) {
        auto warmup = runtime::warmup_renderer(*renderer, composition, runtime::RendererWarmupOptions{
            .width = composition.width(),
            .height = composition.height(),
            .framebuffer_count = args.warmup_framebuffers,
            .preallocate_framebuffers = true,
            .touch_memory = true,
            .render_dummy_frame = args.warmup_dummy_frame,
            .dummy_frame = 0,
            .quiet = args.quiet
        });
        if (!args.quiet) {
            spdlog::info("Renderer warmup: {} framebuffers, {} bytes, {:.2f} ms",
                         warmup.framebuffers_created, warmup.pool_bytes_after, warmup.elapsed_ms);
        }
        if (renderer->counters()) {
            renderer->counters()->setup_pool_preallocation_ms.fetch_add(
                static_cast<uint64_t>(std::llround(warmup.elapsed_ms)), std::memory_order_relaxed);
        }
    }

    for (int i = 0; i < args.warmup; ++i) {
        const auto frame = static_cast<Frame>(i);
        auto scene = composition.evaluate(frame);
        renderer->render_scene(scene, composition.camera, composition.width(), composition.height());
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

    // Clear per-event telemetry stores after warmup, since atomic counters
    // were reset above.  This keeps per-node telemetry in sync with atomic
    // counters like nodes_executed and composite_calls.
    chronon3d::telemetry::clear_telemetry_stores();

    BenchRuntimeContext context;
    context.composition = std::move(composition);
    context.renderer = std::move(renderer);
    context.frames = args.frames;
    context.warmup = args.warmup;
    context.dirty_rects = args.dirty_rects;

    g_bench_context = &context;

    benchmark::AddCustomContext("comp_id", args.comp_id);
    benchmark::AddCustomContext("frames", std::to_string(args.frames));
    benchmark::AddCustomContext("warmup", std::to_string(args.warmup));
    benchmark::AddCustomContext("use_modular_graph", args.use_modular_graph ? "true" : "false");
    benchmark::AddCustomContext("dirty_rects", args.dirty_rects ? "true" : "false");
    benchmark::AddCustomContext("warmup_renderer", args.warmup_renderer ? "true" : "false");

    std::optional<std::filesystem::path> temp_json_path;
    std::filesystem::path current_json_path = args.json_file.empty() ? std::filesystem::path{} : std::filesystem::path(args.json_file);
    if (current_json_path.empty() || !args.compare_file.empty()) {
        temp_json_path = make_temp_json_path();
        if (!temp_json_path) {
            spdlog::error("Failed to allocate temporary benchmark output path");
            g_bench_context = nullptr;
            return 1;
        }
        if (current_json_path.empty()) {
            current_json_path = *temp_json_path;
        }
    }

    auto benchmark_argv_storage = build_benchmark_argv(args, current_json_path);
    std::vector<char*> benchmark_argv;
    benchmark_argv.reserve(benchmark_argv_storage.size());
    for (auto& arg : benchmark_argv_storage) {
        benchmark_argv.push_back(arg.data());
    }

    int benchmark_argc = static_cast<int>(benchmark_argv.size());
    benchmark::Initialize(&benchmark_argc, benchmark_argv.data());

    benchmark::ConsoleReporter display_reporter(
        args.quiet ? benchmark::ConsoleReporter::OO_None : benchmark::ConsoleReporter::OO_Defaults);
    NullStream null_stream;
    if (args.quiet) {
        display_reporter.SetOutputStream(&null_stream);
        display_reporter.SetErrorStream(&null_stream);
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
    if (!args.quiet) {
        spdlog::info("Benchmark JSON written to {}", current_json_path.string());
    }

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
            if (!args.quiet) {
                fmt::print("--- Baseline Comparison ---\n");
            }
            print_comparison(current_rows, baseline_rows, args.fail_if_avg_slower_pct, args.quiet, exit_code);
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

    g_bench_context = nullptr;
    return exit_code;
}

} // namespace cli
} // namespace chronon3d
