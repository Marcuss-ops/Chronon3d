#pragma once

#ifdef CHRONON3D_BUILD_BENCHMARKS

#include "../args.hpp"

#include <benchmark/benchmark.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace chronon3d::cli::detail {

class ScopedSpdlogLevel final {
public:
    explicit ScopedSpdlogLevel(spdlog::level::level_enum level);
    ~ScopedSpdlogLevel();

private:
    spdlog::level::level_enum previous_;
};

struct BenchRuntimeContext {
    std::shared_ptr<const CompiledComposition> compiled;
    std::shared_ptr<SoftwareRenderer> renderer;
    int frames{0};
    int warmup{0};
    bool no_dirty_rects{false};
};

struct BenchmarkSummary {
    std::string name;
    double real_time_ms{0.0};
    double cpu_time_ms{0.0};
    int64_t iterations{0};
};

extern thread_local BenchRuntimeContext* g_bench_context;

[[nodiscard]] std::unique_ptr<std::ostream> make_null_stream();
[[nodiscard]] std::optional<std::filesystem::path> make_temp_json_path();
[[nodiscard]] std::vector<BenchmarkSummary> load_grouped_summaries(
    const std::filesystem::path& path);
void run_render_benchmark(benchmark::State& state);
[[nodiscard]] std::vector<std::string> build_benchmark_argv(
    const BenchArgs& args,
    const std::filesystem::path& json_out_path);
void print_comparison(const std::vector<BenchmarkSummary>& current,
                      const std::vector<BenchmarkSummary>& baseline,
                      double fail_if_avg_slower_pct,
                      bool quiet,
                      int& exit_code);

} // namespace chronon3d::cli::detail

#endif
