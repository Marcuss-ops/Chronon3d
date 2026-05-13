#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <unordered_map>

namespace chronon3d::graph {

class GraphExecutor {
public:
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx
    );

private:
    std::unordered_map<GraphNodeId, std::shared_ptr<Framebuffer>> m_temp;

    std::shared_ptr<Framebuffer> execute_node(
        RenderGraph& graph,
        GraphNodeId id,
        RenderGraphContext& ctx
    );
};

} // namespace chronon3d::graph
