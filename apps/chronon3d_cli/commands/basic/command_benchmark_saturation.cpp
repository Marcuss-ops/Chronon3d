// command_benchmark_saturation — `chronon benchmark --saturation`

#include "../../commands.hpp"
#include "../../cli_context.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include "../../utils/benchmark/benchmark_corpus.hpp"

#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/simd/cpu_isa.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

#include "command_benchmark_saturation_support.inc"
#include "command_benchmark_saturation_run.inc"

} // namespace cli
} // namespace chronon3d
