// =============================================================================
// precomp_builder_service.cpp
//
// TICKET-010 — implementation of `DefaultPrecompBuilder`, the default
// `PrecompBuilderService` installed into `PipelineCatalogs::precomp_builder`.
// Behaviour-equivalent to the legacy std::function wired by
// `wire_precomp_build_factory` (register_pipeline_nodes.cpp):
//
//   1. GraphBuilder::build(scene, nested_ctx) → RenderGraph
//   2. FrameGraphCompiler::compile(graph,  nested_ctx, compile_opts)
//
// The compiler takes the context by reference and may mutate
// `nested_ctx.telemetry.counters` / `nested_ctx.options.*` to surface
// timing and compile flags.  We do NOT introduce any new state semantics
// here — the goal of TICKET-010 is purely to replace the untyped
// std::function callback with a typed service.
// =============================================================================

#include <chronon3d/render_graph/builder/precomp_builder_service.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/scene_binding.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>

namespace chronon3d::graph {

std::unique_ptr<CompiledSceneProgram>
DefaultPrecompBuilder::build(
    const chronon3d::Scene& scene,
    RenderGraphContext& nested_ctx) const
{
    RenderGraph nested_graph = GraphBuilder::build(scene, nested_ctx);

    FrameGraphCompiler compiler;
    FrameGraphCompileOptions compile_opts;
    compile_opts.run_optimizer     = true;
    compile_opts.compute_lifetimes  = true;
    compile_opts.compute_bboxes     = true;

    auto compiled = compiler.compile(std::move(nested_graph), nested_ctx, compile_opts);
    return std::make_unique<CompiledSceneProgram>(
        compile_scene_program(std::move(compiled)));
}

} // namespace chronon3d::graph
