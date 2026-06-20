#include "graph_builder_pipeline.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>

namespace chronon3d::graph {

namespace {

[[nodiscard]] const PipelineCatalogs& builtin_pipeline_catalogs() {
    static const PipelineCatalogs catalogs = [] {
        PipelineCatalogs c;
        init_graph_pipeline_catalogs(c);
        return c;
    }();
    return catalogs;
}

} // namespace

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    CHRONON_ZONE_C("build_render_graph", trace_category::kGraph);
    auto mutable_ctx = ctx;
    const auto& catalogs = builtin_pipeline_catalogs();

    if (!mutable_ctx.resources.node_catalog) {
        mutable_ctx.resources.node_catalog = &catalogs.graph_nodes;
    }
    if (!mutable_ctx.resources.effect_catalog) {
        mutable_ctx.resources.effect_catalog = &catalogs.effects;
    }

    GraphBuildPipeline pipeline;
    pipeline.add_default_passes();
    return pipeline.build(scene, mutable_ctx);
}

} // namespace chronon3d::graph
