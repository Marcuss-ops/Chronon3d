#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <unordered_map>

namespace chronon3d::graph {

class GraphExecutor {
public:
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
    std::unordered_map<GraphNodeId, std::shared_ptr<Framebuffer>> m_temp;
    std::unordered_map<GraphNodeId, u64> m_resolved_key_digest;

    std::shared_ptr<Framebuffer> execute_node(
        RenderGraph& graph,
        GraphNodeId id,
        RenderGraphContext& ctx
    );
};

} // namespace chronon3d::graph
