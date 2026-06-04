#include "graph_builder_pipeline.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    CHRONON_ZONE_C("build_render_graph", trace_category::kGraph);
    auto mutable_ctx = ctx;

    GraphBuildPipeline pipeline;
    pipeline.add_default_passes();
    return pipeline.build(scene, mutable_ctx);
}

} // namespace chronon3d::graph
