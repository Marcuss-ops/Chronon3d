#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// pipeline_catalogs.hpp — Owned catalogs for the render pipeline.
//
// PipelineCatalogs is a plain value type that groups the domain catalogs
// (graph nodes, effects, extensions) + the typed
// `PrecompBuilderService` (replaces the legacy `RenderResourceContext::precomp_build`
// std::function — TICKET-010).
//
// Constructed once by the host (engine lifetime), then passed by const-ref
// to the graph pipeline (graph_cache_coordinator, wire_catalog_pointers).
// No static mutable state.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/render_graph/builder/precomp_builder_service.hpp>

#include <memory>

namespace chronon3d {
    struct ExtensionContext;
}

namespace chronon3d::graph {

/// Grouped domain catalogs + typed builder for a render pipeline.
/// Constructed by the host, then passed by const-ref to the pipeline +
/// graph coordinator.
struct PipelineCatalogs {
    GraphNodeCatalog             graph_nodes;
    effects::EffectCatalog       effects;
    LayerTransitionCatalog       transition_catalog;
    ExtensionCatalog             extensions;

    /// TICKET-010 — typed builder that produces a `CompiledSceneProgram`
    /// for a nested composition (consumed by PrecompNode on cache miss).
    /// Default initialised to `DefaultPrecompBuilder`.  Tests / custom
    /// compiler/profiler instrumentation can replace it with a subclass.
    std::unique_ptr<PrecompBuilderService> precomp_builder{
        std::make_unique<DefaultPrecompBuilder>()};
};

/// Populate and freeze the built-in pipeline catalogs.
///
/// The host creates PipelineCatalogs and ExtensionContext, then calls
/// this function to register pipeline nodes, content modules, etc.
///
///     PipelineCatalogs catalogs;
///     ExtensionContext ctx{registry, catalogs.graph_nodes,
///                          catalogs.effects, assets};
///     populate_builtin_pipeline_catalogs(catalogs, ctx);
///
/// Registers:
///   - Pipeline graph-node factories (source.precomp)
///   - Built-in effect descriptors
///   - Content extension modules (via ctx.compositions)
///
/// Freezes catalogs.graph_nodes and catalogs.effects before returning.
/// `catalogs.precomp_builder` is left untouched (default
/// `DefaultPrecompBuilder` unless the host has overridden it).
void populate_builtin_pipeline_catalogs(PipelineCatalogs& catalogs,
                                         ExtensionContext& ctx);

/// Populate only graph nodes + effects (no content/compositions).
/// Used by the graph pipeline coordinator which doesn't need a
/// CompositionRegistry.
///
/// Registers pipeline graph-node factories and freezes catalogs.
void init_graph_pipeline_catalogs(PipelineCatalogs& catalogs);

/// Wire catalog pointers (graph_nodes + effects) and the typed
/// `PrecompBuilderService*` into `ctx.services`.  TICKET-010 replaces
/// the legacy `wire_precomp_build_factory(ctx, catalogs)` lambda wiring
/// with this typed helper.
///
/// When called with `ctx.services.precomp_builder == nullptr` already set
/// (e.g. the host installed a custom builder post-init), this helper
/// does NOT overwrite it.
void wire_catalog_pointers(RenderGraphContext& ctx, const PipelineCatalogs& catalogs);

} // namespace chronon3d::graph
