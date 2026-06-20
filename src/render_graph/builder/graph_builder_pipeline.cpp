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

    // TICKET-010 — wire catalog pointers + typed PrecompBuilderService
    // via the canonical helper (keeps `graph_cache_coordinator.cpp` and
    // `graph_builder_pipeline.cpp` in lock-step).  The helper preserves
    // any pre-set `ctx.services.precomp_builder` for custom compilers.
    wire_catalog_pointers(mutable_ctx, catalogs);

    GraphBuildPipeline pipeline;
    pipeline.add_default_passes();
    return pipeline.build(scene, mutable_ctx);
}

} // namespace chronon3d::graph
