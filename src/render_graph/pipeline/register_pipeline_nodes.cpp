#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/scene_binding.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <stdexcept>

// Forward declaration from content/register_content_modules.hpp.
namespace chronon3d {
    void register_content_modules(ExtensionCatalog& catalog);
}

namespace chronon3d::graph {

namespace {

/// ExtensionModule wrapping pipeline graph node registration.
/// Takes the node catalog by reference so it can register factories
/// without reaching into static globals.
class PipelineExtension final : public ExtensionModule {
public:
    explicit PipelineExtension(GraphNodeCatalog& node_catalog)
        : m_node_catalog(&node_catalog) {}

    [[nodiscard]] std::string_view name() const override { return "pipeline"; }

    void register_all() override {
        register_pipeline_graph_nodes(*m_node_catalog);
    }

private:
    GraphNodeCatalog* m_node_catalog;
};

} // namespace

void register_pipeline_graph_nodes(GraphNodeCatalog& node_catalog) {
    if (node_catalog.contains("source.precomp")) {
        return;  // already registered
    }

    node_catalog.register_node(GraphNodeDescriptor{
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
}

PipelineCatalogs make_builtin_pipeline_catalogs() {
    PipelineCatalogs catalogs;

    // ── Pipeline graph-node extension ────────────────────────────
    catalogs.extensions.register_module(
        std::make_unique<PipelineExtension>(catalogs.graph_nodes));

    // ── Content extensions ───────────────────────────────────────
    register_content_modules(catalogs.extensions);

    // ── Run all extension modules ────────────────────────────────
    catalogs.extensions.register_all();

    // ── Freeze domain catalogs ───────────────────────────────────
    catalogs.graph_nodes.freeze();
    catalogs.effects.freeze();

    return catalogs;
}

void wire_precomp_build_factory(RenderGraphContext& ctx, const PipelineCatalogs& catalogs) {
    ctx.resources.node_catalog   = &catalogs.graph_nodes;
    ctx.resources.effect_catalog = &catalogs.effects;
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
