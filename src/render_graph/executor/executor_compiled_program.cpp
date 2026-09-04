#include "executor_compiled_program.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <spdlog/spdlog.h>

#include <atomic>

namespace chronon3d::graph {

bool execute_compiled_program(
    const CompiledFrameProgram& program,
    RenderGraphContext& ctx,
    ExecutionState& state,
    RenderCounters* counters)
{
    (void)state;
    auto* backend = ctx.services.backend;
    if (!backend) {
        spdlog::error("[compiled-executor] fully_recorded program has no render backend");
        return false;
    }

    for (const auto& level : program.levels) {
        for (GraphNodeId node_id : level) {
            const CompiledOperation* op = program.operation_for(node_id);
            if (!op || !op->has_compiled_execute()) {
                spdlog::error(
                    "[compiled-executor] node {} has no compiled_execute in "
                    "a fully_recorded program",
                    static_cast<int>(node_id));
                return false;
            }

            if (counters) {
                counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
            }

            if (!op->compiled_execute(backend, *op)) {
                spdlog::error(
                    "[compiled-executor] compiled_execute failed for node {}",
                    static_cast<int>(node_id));
                return false;
            }
        }
    }
    return true;
}

} // namespace chronon3d::graph
