#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/render_graph/builder/graph_build_registry.hpp>
#include "passes/graph_builder_passes.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <stdexcept>

namespace chronon3d::graph {

// ── GraphBuildPipeline ───────────────────────────────────────────────

void GraphBuildPipeline::add_pass(std::unique_ptr<GraphBuildPass> pass) {
    if (!pass) {
        throw std::invalid_argument("GraphBuildPipeline::add_pass: null pass");
    }
    m_passes.push_back(std::move(pass));
}

void GraphBuildPipeline::add_default_passes() {
    // Phase 5: delegate to GraphBuildRegistry which holds the canonical
    // pass sequence.  This allows feature modules to register additional
    // passes without modifying this file.
    auto& registry = GraphBuildRegistry::instance();
    registry.register_default_passes();
    registry.apply_to_pipeline(*this);
}

RenderGraph GraphBuildPipeline::build(const Scene& scene, RenderGraphContext& ctx) {
    CHRONON_ZONE_C("GraphBuildPipeline::build", trace_category::kGraph);

    RenderGraph graph;
    GraphBuildContext build_ctx;
    build_ctx.scene       = &scene;
    build_ctx.render_ctx  = &ctx;
    build_ctx.graph       = &graph;

    return run_passes(build_ctx);
}

RenderGraph GraphBuildPipeline::build_with_resolved(
    const Scene& scene, RenderGraphContext& ctx,
    const GraphBuildContext::ResolvedData& resolved)
{
    CHRONON_ZONE_C("GraphBuildPipeline::build_with_resolved", trace_category::kGraph);

    RenderGraph graph;
    GraphBuildContext build_ctx;
    build_ctx.scene       = &scene;
    build_ctx.render_ctx  = &ctx;
    build_ctx.graph       = &graph;

    // Pre-populate resolved data so ResolvePass skips re-resolution.
    build_ctx.resolved = resolved;
    build_ctx.resolved_prepopulated = true;

    return run_passes(build_ctx);
}

RenderGraph GraphBuildPipeline::run_passes(GraphBuildContext& build_ctx)
{
    for (auto& pass : m_passes) {
        CHRONON_ZONE_C(pass->name().data(), trace_category::kGraph);
        pass->run(build_ctx);
    }

    return std::move(*build_ctx.graph);
}

} // namespace chronon3d::graph
