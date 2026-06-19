#pragma once

namespace chronon3d::graph {

struct RenderGraphContext;

class GraphNodeCatalog;

/// Get the pipeline-level node catalog (populated by register_pipeline_graph_nodes).
const GraphNodeCatalog& get_pipeline_node_catalog();

/// Register pipeline-specific graph node factories in the pipeline catalog.
///
/// Called once before building any graph.  Idempotent — subsequent calls
/// are no-ops.  Must be called from graph_pipeline (not graph_builder)
/// to avoid reintroducing the CMake dependency cycle.
void register_pipeline_graph_nodes();

/// Wire the precomp_build factory into the RenderGraphContext so that
/// PrecompNode (in graph_nodes) can build + compile nested scenes without
/// directly linking graph_builder or graph_compiler.
void wire_precomp_build_factory(RenderGraphContext& ctx);

} // namespace chronon3d::graph
