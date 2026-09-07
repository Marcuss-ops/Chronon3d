#include "graph_builder_pipeline.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    CHRONON_TRACE_SCOPE("chronon.graph", "build_render_graph");
    auto mutable_ctx = ctx;

    GraphBuildPipeline pipeline;
    pipeline.add_default_passes();
    return pipeline.build(scene, mutable_ctx);
}

} // namespace chronon3d::graph
