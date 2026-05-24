#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <tbb/task_arena.h>
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

private:
    struct ExecutionPlan {
        std::vector<std::vector<GraphNodeId>> levels;
        std::vector<size_t> consumer_counts;
    };

    tbb::task_arena m_arena;
    FrameArena      m_frame_arena;

    [[nodiscard]] ExecutionPlan build_execution_plan(RenderGraph& graph, GraphNodeId output) const;
};

} // namespace chronon3d::graph
