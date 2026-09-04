// Render graph DAG executor wiring.
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include "executor_levels.hpp"
#include "framebuffer_lifetime.hpp"
#include <chronon3d/render_graph/core/graph_profiler.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/gpu_device_lost_error.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chronon3d::graph {

#include "executor_compiled_program_detail.hpp"
#include "executor_internal_detail.hpp"
#include "executor_api_detail.hpp"

} // namespace chronon3d::graph
