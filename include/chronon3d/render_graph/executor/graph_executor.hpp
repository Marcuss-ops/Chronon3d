#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/core/memory/arena.hpp>
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
    /// @param arena_override  Optional external arena for temporary allocations.
    ///        When provided, the executor uses this arena instead of its internal
    ///        m_frame_arena, allowing multiple execute() calls to run concurrently
    ///        on the same executor instance without data races on the shared arena.
    ///        Each concurrent caller MUST provide its own arena.
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx,
        FrameArena* arena_override = nullptr
    );

    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        RenderGraphContext& ctx,
        FrameArena* arena_override = nullptr
    ) {
        return execute(graph, graph.output(), ctx, arena_override);
    }

    std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        FrameArena* arena_override = nullptr
    );

    /// Invalidate the cached execution plan — call this after the graph topology
    /// changes (nodes added/removed/reconnected) between execute() calls.
    void invalidate_plan_cache() {
        std::lock_guard<std::mutex> lock(m_plan_mutex);
        m_cached_plan.valid = false;
    }

private:
    struct ExecutionPlan {
        std::vector<std::vector<GraphNodeId>> levels;
        std::vector<size_t> consumer_counts;
    };

    struct CachedExecutionPlan {
        ExecutionPlan plan;
        std::uint64_t structure_hash{0};
        GraphNodeId output{k_invalid_node};
        bool valid{false};
    };

    tbb::task_arena m_arena;
    FrameArena      m_frame_arena;
    mutable std::mutex m_plan_mutex;
    CachedExecutionPlan m_cached_plan;

    [[nodiscard]] ExecutionPlan build_execution_plan(RenderGraph& graph, GraphNodeId output) const;

    /// Compute a hash that uniquely identifies the graph topology:
    /// node kinds, input connectivity, and output node ID.
    [[nodiscard]] static std::uint64_t compute_structure_signature(
        const RenderGraph& graph, GraphNodeId output);
};

} // namespace chronon3d::graph
