#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <robin_hood.h>
#include <enkiTS/TaskScheduler.h>

namespace chronon3d::graph {

class GraphExecutor {
public:
    GraphExecutor();

    // Execute from an explicit output node.
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx
    );

    // Execute using the graph's declared output node (preferred).
    std::shared_ptr<Framebuffer> execute(RenderGraph& graph, RenderGraphContext& ctx) {
        return execute(graph, graph.output(), ctx);
    }

private:
    enki::TaskScheduler m_scheduler;
    robin_hood::unordered_flat_map<GraphNodeId, std::shared_ptr<Framebuffer>> m_temp;
    robin_hood::unordered_flat_map<GraphNodeId, u64> m_resolved_key_digest;

    std::shared_ptr<Framebuffer> execute_node(
        RenderGraph& graph,
        GraphNodeId id,
        RenderGraphContext& ctx
    );
};

} // namespace chronon3d::graph
