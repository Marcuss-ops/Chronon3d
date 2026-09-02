#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/runtime/frame_execution_result.hpp>

#include <cstdint>
#include <memory>

namespace chronon3d::graph {

struct FrameExecutionOutput {
    std::shared_ptr<Framebuffer> framebuffer{};
    runtime::FrameExecutionResult result{};

    [[nodiscard]] bool ok() const noexcept {
        return framebuffer != nullptr && result.ok();
    }
};

class GraphExecutor {
public:
    GraphExecutor() = default;

    /// Explicit frame/job failure boundary. This is the canonical API for
    /// callers that need to distinguish an engine failure from an empty frame.
    /// The session's existing NodeExecutionError slot remains the sole stored
    /// diagnostic authority; the result is derived from it after execution.
    [[nodiscard]] FrameExecutionOutput execute_with_result(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler
    ) const;

    [[nodiscard]] FrameExecutionOutput execute_with_scope_result(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        ExecutionScope& scope,
        ExecutionScheduler& scheduler
    ) const;

    /// Compatibility entrypoint. Delegates to execute_with_result() so there
    /// is only one execution/failure path.
    [[deprecated("use execute_with_scope_result(ExecutionScope&, ExecutionScheduler&) for an explicit failure result")]]
    [[nodiscard]] std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler
    ) const;

    /// Compatibility framebuffer-only scope entrypoint. New production code
    /// should consume execute_with_scope_result().
    [[nodiscard]] std::shared_ptr<Framebuffer> execute_with_scope(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        ExecutionScope& scope,
        ExecutionScheduler& scheduler
    ) const;
};

} // namespace chronon3d::graph
