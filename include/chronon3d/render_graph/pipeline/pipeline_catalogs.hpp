#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// pipeline_catalogs.hpp — Owned catalogs for the render pipeline.
//
// PipelineCatalogs is a plain value type that groups the three domain
// catalogs (graph nodes, effects, extensions).  It is constructed once
// by make_builtin_pipeline_catalogs(ctx) and then passed by const-ref to
// the graph pipeline (graph_cache_coordinator, wire_precomp_build_factory).
//
// No static mutable state — the catalogs are owned by the caller.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/extension/extension_catalog.hpp>

namespace chronon3d {
    struct ExtensionContext;
}

namespace chronon3d::graph {

/// Grouped domain catalogs for a render pipeline.
/// Constructed by make_builtin_pipeline_catalogs(), then passed by
/// const-ref to the pipeline and graph coordinator.
struct PipelineCatalogs {
    GraphNodeCatalog       graph_nodes;
    effects::EffectCatalog effects;
    ExtensionCatalog       extensions;
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
void populate_builtin_pipeline_catalogs(PipelineCatalogs& catalogs,
                                         ExtensionContext& ctx);

/// Populate only graph nodes + effects (no content/compositions).
/// Used by the graph pipeline coordinator which doesn't need a
/// CompositionRegistry.
///
/// Registers pipeline graph-node factories and freezes catalogs.
void init_graph_pipeline_catalogs(PipelineCatalogs& catalogs);

} // namespace chronon3d::graph
