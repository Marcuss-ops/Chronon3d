// =============================================================================
// register_pipeline_nodes.cpp — Pipeline catalog population + ctx wiring.
//
// TICKET-010 — replaces the legacy std::function callback
// `wire_precomp_build_factory(ctx, catalogs)` with a typed helper
// `wire_catalog_pointers(ctx, catalogs)` that copies catalog pointers +
// a typed `PrecompBuilderService*` into `ctx.services`.  The actual
// compile/optimise code is no longer here — it lives in
// `DefaultPrecompBuilder::build()` (precomp_builder_service.cpp).  This
// separation lets us:
//   - Unit-test the build pipeline independently of the catalog wiring.
//   - Substitute a custom builder (e.g. specialised compiler/profiler
//     instrumentation) without touching the catalog init code.
// =============================================================================

#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <stdexcept>

#if defined(CHRONON3D_BUILD_CONTENT)
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
                spec->cache_policy
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
#if defined(CHRONON3D_BUILD_CONTENT)
    register_content_modules(catalogs.extensions, ctx);
#endif

    // ── Run all extension modules ────────────────────────────────
    catalogs.extensions.register_all(ctx);

    // ── Freeze domain catalogs ───────────────────────────────────
    catalogs.graph_nodes.freeze();
    catalogs.effects.freeze();
    // catalogs.precomp_builder is left at its default (DefaultPrecompBuilder)
    // unless the host has overridden it before this call.
}

void init_graph_pipeline_catalogs(PipelineCatalogs& catalogs) {
    // ── Pipeline graph-node extension ────────────────────────────
    catalogs.extensions.register_module(
        std::make_unique<PipelineExtension>());

    // ── Register pipeline graph nodes (no content/compositions) ──
    register_pipeline_graph_nodes(catalogs.graph_nodes);

    // ── Register built-in layer transitions ──────────────────────
    LayerTransitionCatalog::register_builtin(catalogs.transition_catalog);

    // ── Freeze domain catalogs ───────────────────────────────────
    catalogs.graph_nodes.freeze();
    catalogs.effects.freeze();
    // catalogs.precomp_builder is left at its default (DefaultPrecompBuilder).
}

void wire_catalog_pointers(RenderGraphContext& ctx, const PipelineCatalogs& catalogs) {
    ctx.services.node_catalog         = &catalogs.graph_nodes;
    ctx.services.effect_catalog       = &catalogs.effects;
    ctx.services.transition_catalog   = &catalogs.transition_catalog;
    // Wire the typed builder only when the caller hasn't already installed
    // a custom service.  Tests / instrumentation can pre-set
    // `ctx.services.precomp_builder` to a non-null substitute before this
    // call and the override will be preserved.
    if (ctx.services.precomp_builder == nullptr && catalogs.precomp_builder) {
        ctx.services.precomp_builder = catalogs.precomp_builder.get();
    }
}

} // namespace chronon3d::graph
