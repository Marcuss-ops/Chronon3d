#pragma once

#include "execution_state.hpp"

#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <memory>

namespace chronon3d::graph {

// Private executor-module orchestration boundary. Compiled-program execution
// has its own src-local interface in executor_compiled_program.hpp.
[[nodiscard]] std::shared_ptr<Framebuffer> execute_internal(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    runtime::JobSession& session,
    FrameArena& arena,
    ExecutionScheduler& scheduler);

} // namespace chronon3d::graph
