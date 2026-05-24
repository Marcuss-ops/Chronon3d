#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include "builder/graph_builder_internal.hpp"
#include "builder/graph_builder_pipeline.hpp"

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    CHRONON_ZONE_C("build_render_graph", trace_category::kGraph);
    auto mutable_ctx = ctx;
    const auto resolved = detail::resolve_layers(scene, mutable_ctx);
    return detail::build_graph(scene, mutable_ctx, resolved);
}

} // namespace chronon3d::graph
