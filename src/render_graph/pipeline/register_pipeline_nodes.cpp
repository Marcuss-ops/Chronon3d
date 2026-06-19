#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/scene_binding.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <stdexcept>

namespace chronon3d::graph {

void register_pipeline_graph_nodes(GraphNodeRegistry& registry) {
    if (registry.contains("source.precomp")) {
        return;  // already registered
    }

    registry.register_node(GraphNodeDescriptor{
        .id = "source.precomp",
        .display_name = "Precomposition",
        .description = "Renders a nested composition",
        .category = "source",
        .builtin = true,
        .factory = [](const GraphNodeCreateRequest& request)
            -> std::unique_ptr<RenderGraphNode>
        {
            const auto* spec =
                std::get_if<PrecompNodeCreateSpec>(&request.payload);

            if (!spec) {
                throw std::invalid_argument(
                    "source.precomp requires PrecompNodeCreateSpec");
            }

            return std::make_unique<PrecompNode>(
                spec->composition_name,
                spec->start_frame,
                spec->duration,
                spec->cache_frame,
                spec->cache_capacity,
                spec->tune_mode,
                spec->tune_interval,
                spec->tune_min_capacity,
                spec->tune_max_capacity
            );
        }
    });

    registry.freeze();
}

void wire_precomp_build_factory(RenderGraphContext& ctx) {
    ctx.resources.precomp_build = [](const Scene& scene, RenderGraphContext& nested_ctx)
        -> std::unique_ptr<CompiledSceneProgram>
    {
        RenderGraph nested_graph = GraphBuilder::build(scene, nested_ctx);

        FrameGraphCompiler compiler;
        FrameGraphCompileOptions compile_opts;
        compile_opts.run_optimizer    = true;
        compile_opts.compute_lifetimes = true;
        compile_opts.compute_bboxes   = true;

        auto compiled = compiler.compile(std::move(nested_graph), nested_ctx, compile_opts);
        return std::make_unique<CompiledSceneProgram>(
            compile_scene_program(std::move(compiled)));
    };
}

} // namespace chronon3d::graph
