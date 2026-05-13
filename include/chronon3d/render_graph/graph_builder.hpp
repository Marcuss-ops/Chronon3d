#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/scene/scene.hpp>

namespace chronon3d::graph {

class GraphBuilder {
public:
    static RenderGraph build(const Scene& scene, const RenderGraphContext& ctx);

private:
    // Builds the source subgraph for a Normal layer by compositing layer.nodes
    // bottom-to-top, each wrapped in a SourceNode. Returns the final node id.
    static GraphNodeId build_layer_source(
        RenderGraph& graph,
        const Layer& layer,
        const RenderGraphContext& ctx
    );
};

} // namespace chronon3d::graph
