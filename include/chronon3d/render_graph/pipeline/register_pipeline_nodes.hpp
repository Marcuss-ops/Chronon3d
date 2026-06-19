#pragma once

namespace chronon3d::graph {

struct RenderGraphContext;
struct PipelineCatalogs;
class GraphNodeCatalog;

/// Register pipeline-specific graph node factories into the given catalog.
///
/// Called once before building any graph.  Idempotent — subsequent calls
/// are no-ops.
void register_pipeline_graph_nodes(GraphNodeCatalog& node_catalog);

/// Wire the precomp_build factory and catalog pointers into the
/// RenderGraphContext so that PrecompNode (in graph_nodes) can build +
/// compile nested scenes without directly linking graph_builder or
/// graph_compiler.
void wire_precomp_build_factory(RenderGraphContext& ctx, const PipelineCatalogs& catalogs);

} // namespace chronon3d::graph
