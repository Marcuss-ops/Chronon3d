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
#include <chronon3d/extension/extension_context.hpp>
#include <stdexcept>

#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
// Forward declaration from content/register_content_modules.hpp.
namespace chronon3d {
    void register_content_modules(ExtensionCatalog& catalog,
                                   ExtensionContext& ctx);
}
#endif

namespace chronon3d::graph {

namespace {

/// ExtensionModule wrapping pipeline graph node registration.
/// Receives ExtensionContext with all domain registries.
class PipelineExtension final : public ExtensionModule {
public:
    [[nodiscard]] std::string_view name() const override { return "pipeline"; }

    void register_all(ExtensionContext& ctx) override {
        register_pipeline_graph_nodes(ctx.graph_nodes);
    }
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

void populate_builtin_pipeline_catalogs(PipelineCatalogs& catalogs,
                                         ExtensionContext& ctx) {
    // ── Pipeline graph-node extension ────────────────────────────
    catalogs.extensions.register_module(
        std::make_unique<PipelineExtension>());

    // ── Content extensions ───────────────────────────────────────
#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
    register_content_modules(catalogs.extensions, ctx);
#endif

    // ── Run all extension modules ────────────────────────────────
    catalogs.extensions.register_all(ctx);

    // ── Freeze domain catalogs ───────────────────────────────────
    catalogs.graph_nodes.freeze();
    catalogs.effects.freeze();
}

void init_graph_pipeline_catalogs(PipelineCatalogs& catalogs) {
    // ── Pipeline graph-node extension ────────────────────────────
    catalogs.extensions.register_module(
        std::make_unique<PipelineExtension>());

    // ── Register pipeline graph nodes (no content/compositions) ──
    // The ExtensionContext here is minimal — only graph_nodes is used.
    // We create a temporary context; the PipelineExtension only
    // uses ctx.graph_nodes.
    // ── No content modules — graph coordinator doesn't need compositions ─

    // ── Run pipeline extension (only graph node registration) ────
    register_pipeline_graph_nodes(catalogs.graph_nodes);

    // ── Freeze domain catalogs ───────────────────────────────────
    catalogs.graph_nodes.freeze();
    catalogs.effects.freeze();
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
