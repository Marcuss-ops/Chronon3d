#include "graph_builder_passes.hpp"
#include "../graph_builder_pipeline.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/scene/model/scene.hpp>

namespace chronon3d::graph::detail {

void RootSourcesPass::run(GraphBuildContext& ctx) {
    auto& graph = *ctx.graph;

    // Start with a ClearNode as the base of the compositing chain.
    GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());

    // Append root-level scene nodes (standalone shapes not in any layer).
    current = detail::append_root_sources(graph, *ctx.scene, *ctx.render_ctx, current);

    ctx.current_output = current;
}

} // namespace chronon3d::graph::detail
