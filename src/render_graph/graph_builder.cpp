#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include "builder/graph_builder_internal.hpp"
#include "builder/graph_builder_pipeline.hpp"
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    CHRONON_ZONE_C("build_render_graph", trace_category::kGraph);
    auto mutable_ctx = ctx;
    const auto resolved = detail::resolve_layers(scene, mutable_ctx);
    auto graph = detail::build_graph(scene, mutable_ctx, resolved);
    std::ignore = optimizer::optimize_graph(graph, mutable_ctx);
    return graph;
}

} // namespace chronon3d::graph
