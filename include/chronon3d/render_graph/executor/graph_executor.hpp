#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/runtime/frame_execution_result.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <memory>

namespace chronon3d::graph {

class RenderBackend;

struct FrameExecutionOutput {
    std::shared_ptr<Framebuffer> framebuffer{};
    runtime::FrameExecutionResult result{};

    [[nodiscard]] bool execution_ok() const noexcept {
        return result.ok();
    }

    [[nodiscard]] bool has_framebuffer() const noexcept {
        return framebuffer != nullptr;
    }

    [[nodiscard]] bool ok() const noexcept {
        return execution_ok();
    }
};

class GraphExecutor {
public:
    GraphExecutor() = default;

    /// Sole graph execution API. ExecutionScope owns the session/arena
    /// authority and FrameExecutionOutput carries both produced output and the
    /// typed frame-execution result.
    [[nodiscard]] FrameExecutionOutput execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        ExecutionScope& scope,
        ExecutionScheduler& scheduler
    ) const;

    /// Execute a pre-lowered CommandPlan through the same canonical backend
    /// surface dispatch used by the graph executor. CommandPlan remains a
    /// synchronization/data contract; GraphExecutor is the sole authority.
    [[nodiscard]] bool execute(
        RenderBackend& backend,
        runtime::RenderSurfaceRegistry& registry,
        const runtime::CommandPlan& plan
    ) const;
};

} // namespace chronon3d::graph
