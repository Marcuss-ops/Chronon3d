#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// pipeline_catalogs.hpp — Owned catalogs for the render pipeline.
//
// PipelineCatalogs is a plain value type that groups the three domain
// catalogs (graph nodes, effects, extensions).  It is constructed once
// by make_builtin_pipeline_catalogs() and then passed by const-ref to
// the graph pipeline (graph_cache_coordinator, wire_precomp_build_factory).
//
// No static mutable state — the catalogs are owned by the caller.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/extension/extension_catalog.hpp>

namespace chronon3d::graph {

/// Grouped domain catalogs for a render pipeline.
/// Constructed by make_builtin_pipeline_catalogs(), then passed by
/// const-ref to the pipeline and graph coordinator.
struct PipelineCatalogs {
    GraphNodeCatalog       graph_nodes;
    effects::EffectCatalog effects;
    ExtensionCatalog       extensions;
};

/// Create and populate the built-in pipeline catalogs.
///
/// Registers:
///   - Pipeline graph-node factories (source.precomp)
///   - Built-in effect descriptors
///   - Content extension modules
///
/// Freezes all catalogs before returning.
[[nodiscard]] PipelineCatalogs make_builtin_pipeline_catalogs();

} // namespace chronon3d::graph
