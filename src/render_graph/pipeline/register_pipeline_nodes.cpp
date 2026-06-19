#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/scene_binding.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <stdexcept>

namespace chronon3d::graph {

// Forward-declared: defined below, called from PipelineExtension::register_all().
void register_pipeline_graph_nodes_impl();

namespace {

/// ExtensionModule wrapping pipeline graph node registration.
class PipelineExtension final : public ExtensionModule {
public:
    [[nodiscard]] std::string_view name() const override { return "pipeline"; }

    void register_all() override {
        register_pipeline_graph_nodes_impl();
    }
};

} // namespace

static GraphNodeCatalog s_pipeline_node_catalog;
static effects::EffectCatalog s_effect_catalog;

const GraphNodeCatalog& get_pipeline_node_catalog() {
    return s_pipeline_node_catalog;
}

const effects::EffectCatalog& get_pipeline_effect_catalog() {
    return s_effect_catalog;
}

void register_pipeline_graph_nodes_impl() {
    if (s_pipeline_node_catalog.contains("source.precomp")) {
        return;  // already registered
    }

    s_pipeline_node_catalog.register_node(GraphNodeDescriptor{
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

    s_pipeline_node_catalog.freeze();
    s_effect_catalog.freeze();
}

void register_pipeline_graph_nodes() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.contains("pipeline")) {
        reg.register_module(std::make_unique<PipelineExtension>());
    }
    reg.register_all();
}

void wire_precomp_build_factory(RenderGraphContext& ctx) {
    ctx.resources.node_catalog = &s_pipeline_node_catalog;
    ctx.resources.effect_catalog = &s_effect_catalog;
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
