#include <chronon3d/compiler/compiler_pass_pipeline.hpp>

#include <chronon3d/compiler/compiler_pass_registry.hpp>
#include "graph_builder/passes/graph_builder_passes.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <stdexcept>

namespace chronon3d::compiler {

void CompilerPassPipeline::add_pass(
    std::unique_ptr<graph::GraphBuildPass> pass) {
    if (!pass) {
        throw std::invalid_argument("CompilerPassPipeline::add_pass: null pass");
    }
    passes_.push_back(std::move(pass));
}

void CompilerPassPipeline::add_default_passes() {
    auto& registry = CompilerPassRegistry::instance();
    registry.register_default_passes();
    registry.apply_to_pipeline(*this);
}

graph::RenderGraph CompilerPassPipeline::build(
    const Scene& scene, graph::RenderGraphContext& context) {
    CHRONON_TRACE_SCOPE("chronon.compiler", "CompilerPassPipeline::build");
    graph::RenderGraph graph;
    graph::GraphBuildContext build_context;
    build_context.scene = &scene;
    build_context.render_ctx = &context;
    build_context.graph = &graph;
    build_context.dependencies = graph::make_graph_build_dependencies(context, scene);
    build_context.cam25d = scene.camera_2_5d();
    context.build_dependencies = &build_context.dependencies;
    auto result = run_passes(build_context);
    context.build_dependencies = nullptr;
    return result;
}

graph::RenderGraph CompilerPassPipeline::build_with_resolved(
    const Scene& scene,
    graph::RenderGraphContext& context,
    const graph::GraphBuildContext::ResolvedData& resolved) {
    CHRONON_TRACE_SCOPE(
        "chronon.compiler", "CompilerPassPipeline::build_with_resolved");
    graph::RenderGraph graph;
    graph::GraphBuildContext build_context;
    build_context.scene = &scene;
    build_context.render_ctx = &context;
    build_context.graph = &graph;
    build_context.dependencies = graph::make_graph_build_dependencies(context, scene);
    build_context.cam25d = scene.camera_2_5d();
    build_context.resolved = resolved;
    build_context.resolved_prepopulated = true;
    context.build_dependencies = &build_context.dependencies;
    auto result = run_passes(build_context);
    context.build_dependencies = nullptr;
    return result;
}

graph::RenderGraph CompilerPassPipeline::run_passes(
    graph::GraphBuildContext& context) {
    for (auto& pass : passes_) {
        CHRONON_TRACE_SCOPE("chronon.compiler", "CompilerPassRun");
        pass->run(context);
    }
    return std::move(*context.graph);
}

}  // namespace chronon3d::compiler
