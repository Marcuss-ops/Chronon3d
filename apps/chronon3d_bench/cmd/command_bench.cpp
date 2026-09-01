#include "../args.hpp"
#include "../../chronon3d_cli/utils/job/cli_render_utils.hpp"

#ifdef CHRONON3D_BUILD_BENCHMARKS
#include <benchmark/benchmark.h>
#endif
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/benchmark_report.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_BUILD_BENCHMARKS

#include <algorithm>
#include <atomic>
#include <chronon3d/core/profiling/profiling.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

#include "command_bench_support.inc"
#include "command_bench_run.inc"

} // namespace cli
} // namespace chronon3d

#else // !CHRONON3D_BUILD_BENCHMARKS

#include "../args.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {
int command_bench(const CompositionRegistry&, const BenchArgs&) {
    spdlog::error("Benchmarks not available: built without Google Benchmark support");
    return 1;
}
} // namespace cli
} // namespace chronon3d

#endif // CHRONON3D_BUILD_BENCHMARKS
