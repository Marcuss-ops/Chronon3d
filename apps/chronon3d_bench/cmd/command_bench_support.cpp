#include "command_bench_internal.hpp"

#ifdef CHRONON3D_BUILD_BENCHMARKS

#include <chronon3d/core/profiling/benchmark_report.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <stdexcept>
#include <streambuf>

namespace chronon3d::cli::detail {
namespace {

class NullBuffer final : public std::streambuf {
protected:
    int overflow(int c) override { return traits_type::not_eof(c); }
};

class NullStream final : public std::ostream {
public:
    NullStream() : std::ostream(&buffer_) {}
private:
    NullBuffer buffer_;
};

std::vector<BenchmarkSummary> load_benchmark_summaries(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) throw std::runtime_error("Failed to open benchmark JSON: " + path.string());
    nlohmann::json js;
    in >> js;
    std::vector<BenchmarkSummary> summaries;
    if (!js.contains("benchmarks") || !js["benchmarks"].is_array()) return summaries;
    for (const auto& entry : js["benchmarks"]) {
        if (!entry.is_object() || entry.value("run_type", "") != "iteration") continue;
        summaries.push_back({
            .name = entry.value("name", ""),
            .real_time_ms = entry.value("real_time", 0.0),
            .cpu_time_ms = entry.value("cpu_time", 0.0),
            .iterations = entry.value("iterations", int64_t{0})});
    }
    return summaries;
}

} // namespace

ScopedSpdlogLevel::ScopedSpdlogLevel(spdlog::level::level_enum level)
    : previous_(spdlog::get_level()) {
    spdlog::set_level(level);
}
ScopedSpdlogLevel::~ScopedSpdlogLevel() { spdlog::set_level(previous_); }

thread_local BenchRuntimeContext* g_bench_context = nullptr;

std::unique_ptr<std::ostream> make_null_stream() {
    return std::make_unique<NullStream>();
}

std::optional<std::filesystem::path> make_temp_json_path() {
    try {
        auto dir = std::filesystem::temp_directory_path();
        auto stamp = std::to_string(static_cast<long long>(profiling::timestamp_ns()));
        return dir / ("chronon3d_bench_" + stamp + ".json");
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<BenchmarkSummary> load_grouped_summaries(const std::filesystem::path& path) {
    auto rows = load_benchmark_summaries(path);
    std::vector<BenchmarkSummary> grouped;
    if (rows.empty()) return grouped;
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
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
    auto* counters = ctx.renderer->counters();
    for (auto _ : state) {
        for (int i = 0; i < ctx.frames; ++i) {
            const auto frame = static_cast<Frame>(ctx.warmup + i);
            ctx.renderer->render_compiled(*ctx.compiled, frame);
        }
        benchmark::DoNotOptimize(counters);
        benchmark::ClobberMemory();
    }
    const double frames = static_cast<double>(state.iterations()) * static_cast<double>(ctx.frames);
    state.SetItemsProcessed(static_cast<int64_t>(frames));
    state.counters["cache_hits"] = static_cast<double>(counters->cache_hits.load(std::memory_order_relaxed));
    state.counters["cache_misses"] = static_cast<double>(counters->cache_misses.load(std::memory_order_relaxed));
    state.counters["nodes_executed"] = static_cast<double>(counters->nodes_executed.load(std::memory_order_relaxed));
    state.counters["pixels_touched"] = static_cast<double>(counters->pixels_touched.load(std::memory_order_relaxed));
    state.counters["blur_pixels"] = static_cast<double>(counters->blur_pixels.load(std::memory_order_relaxed));
    state.counters["images_sampled"] = static_cast<double>(counters->images_sampled.load(std::memory_order_relaxed));
    state.counters["text_glyphs_rasterized"] = static_cast<double>(counters->text_glyphs_rasterized.load(std::memory_order_relaxed));
    const auto hits = counters->cache_hits.load(std::memory_order_relaxed);
    const auto misses = counters->cache_misses.load(std::memory_order_relaxed);
    state.counters["cache_hit_rate"] = hits + misses > 0
        ? static_cast<double>(hits) / static_cast<double>(hits + misses) : 0.0;
}

std::vector<std::string> build_benchmark_argv(
    const BenchArgs& args,
    const std::filesystem::path& json_out_path) {
    std::vector<std::string> argv{
        "chronon3d_bench",
        "--benchmark_min_time=0.1s",
        "--benchmark_repetitions=1",
        "--benchmark_report_aggregates_only=false",
        "--benchmark_display_aggregates_only=false",
        "--benchmark_out=" + json_out_path.string(),
        "--benchmark_out_format=json"};
    if (args.quiet) argv.emplace_back("--benchmark_color=false");
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
        if (it == baseline.end()) continue;
        any_match = true;
        const double regression_pct = compute_regression_pct(it->real_time_ms, cur.real_time_ms);
        if (!quiet) {
            fmt::print("  - {:32}: baseline={:10.3f} ms  current={:10.3f} ms  delta={:+7.2f}%\n",
                       cur.name, it->real_time_ms, cur.real_time_ms, regression_pct);
        }
        if (fail_if_avg_slower_pct > 0.0 && regression_pct > fail_if_avg_slower_pct) {
            spdlog::error("Benchmark regression for {}: {:.2f}% (threshold: {:.2f}%)",
                          cur.name, regression_pct, fail_if_avg_slower_pct);
            exit_code = 2;
        }
    }
    if (!any_match && !quiet) spdlog::warn("No benchmark names matched between current run and baseline");
}

} // namespace chronon3d::cli::detail

#endif
