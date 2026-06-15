#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/core/memory/render_session.hpp>
#include <tbb/task_arena.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace chronon3d::graph {

class GraphExecutor {
public:
    GraphExecutor();

    /// Execute a render graph.
    /// @param session  The RenderSession providing the frame arena and per-frame state.
    /// @param arena_override  Optional external arena override for temporary allocations.
    ///        When provided, the executor uses this arena instead of session.arena,
    ///        allowing tile-execution paths to supply a short-lived local arena.
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx,
        RenderSession& session,
        FrameArena* arena_override = nullptr
    );

    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        RenderGraphContext& ctx,
        RenderSession& session
    ) {
        return execute(graph, graph.output(), ctx, session);
    }

    std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        FrameArena* arena_override = nullptr
    );

    /// Invalidate the cached execution plan — call this after the graph topology
    /// changes (nodes added/removed/reconnected) between execute() calls.
    void invalidate_plan_cache() {
        std::lock_guard<std::mutex> lock(m_plan_mutex);
        m_cached_plan.valid = false;
        m_cached_plan.plan.reset();
    }

private:
    struct ExecutionPlan {
        std::vector<std::vector<GraphNodeId>> levels;
        std::vector<size_t> consumer_counts;
    };

    struct CachedExecutionPlan {
        std::shared_ptr<const ExecutionPlan> plan;
        std::uint64_t structure_hash{0};
        GraphNodeId output{k_invalid_node};
        bool valid{false};
    };

    tbb::task_arena m_arena;
    // m_frame_arena removed — moved to RenderSession
    mutable std::mutex m_plan_mutex;
    CachedExecutionPlan m_cached_plan;

    [[nodiscard]] std::shared_ptr<const ExecutionPlan> build_execution_plan(RenderGraph& graph, GraphNodeId output) const;

    /// Compute a hash that uniquely identifies the graph topology:
    /// node kinds, input connectivity, and output node ID.
    [[nodiscard]] static std::uint64_t compute_structure_signature(
        const RenderGraph& graph, GraphNodeId output);
};

} // namespace chronon3d::graph
