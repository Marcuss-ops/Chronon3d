#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/runtime/frame_execution_result.hpp>

#include <memory>

namespace chronon3d::graph {

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
};

} // namespace chronon3d::graph
