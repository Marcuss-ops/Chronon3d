#include <chronon3d/render_graph/graph_builder.hpp>

#include "graph_builder_internal.hpp"
#include "graph_builder_pipeline.hpp"

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    const auto resolved = detail::resolve_layers(scene, ctx);
    return detail::build_graph(scene, ctx, resolved);
}

} // namespace chronon3d::graph
