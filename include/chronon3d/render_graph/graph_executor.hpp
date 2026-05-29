#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <tbb/task_arena.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace chronon3d::graph {

class GraphExecutor {
public:
    GraphExecutor();

    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx
    );

    std::shared_ptr<Framebuffer> execute(RenderGraph& graph, RenderGraphContext& ctx) {
        return execute(graph, graph.output(), ctx);
    }

    /// Invalidate the cached execution plan — call this after the graph topology
    /// changes (nodes added/removed/reconnected) between execute() calls.
    void invalidate_plan_cache() { m_cached_plan.valid = false; }

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
    CachedExecutionPlan m_cached_plan;

    [[nodiscard]] ExecutionPlan build_execution_plan(RenderGraph& graph, GraphNodeId output) const;

    /// Compute a hash that uniquely identifies the graph topology:
    /// node kinds, input connectivity, and output node ID.
    [[nodiscard]] static std::uint64_t compute_structure_signature(
        const RenderGraph& graph, GraphNodeId output);
};

} // namespace chronon3d::graph
