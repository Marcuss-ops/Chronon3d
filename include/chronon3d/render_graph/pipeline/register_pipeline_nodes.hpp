#pragma once

namespace chronon3d::graph {

/// Register pipeline-specific graph node factories in the GraphNodeRegistry.
///
/// Called once before building any graph.  Idempotent — subsequent calls
/// are no-ops.  Must be called from graph_pipeline (not graph_builder)
/// to avoid reintroducing the CMake dependency cycle.
void register_pipeline_graph_nodes();

} // namespace chronon3d::graph
