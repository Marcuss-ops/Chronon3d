#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// register_pipeline_nodes.hpp — Internal API for populating pipeline catalogs
//                              and wiring catalog pointers into the
//                              RenderGraphContext.
//
// TICKET-010 — the legacy `wire_precomp_build_factory(ctx, catalogs)` lambda
// has been replaced by `wire_catalog_pointers(ctx, catalogs)` (see
// pipeline_catalogs.hpp).  PrecompNode now invokes
// `ctx.services.precomp_builder->build(...)` — a typed call against the
// service owned by `catalogs.precomp_builder` (a unique_ptr).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>

namespace chronon3d::graph {

class RenderGraphNode;

/// Register the pipeline's graph-node factories into the provided catalog.
/// Currently registers `source.precomp` (PrecompNode).
///
/// Idempotent: re-calling on a catalog that already contains the registration
/// is a no-op.
void register_pipeline_graph_nodes(GraphNodeCatalog& node_catalog);

} // namespace chronon3d::graph
