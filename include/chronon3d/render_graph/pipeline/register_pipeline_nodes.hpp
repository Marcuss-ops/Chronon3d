#pragma once

#include <chronon3d/render_graph/registry/graph_node_registry.hpp>

namespace chronon3d::graph {

struct RenderGraphContext;

/// Register pipeline-specific graph node factories in the passed registry.
///
/// Called once before building any graph — typically from the renderer
/// constructor.  Idempotent — subsequent calls are no-ops.
void register_pipeline_graph_nodes(GraphNodeRegistry& registry);

/// Wire the precomp_build factory (and effect catalog pointer) into
/// the RenderGraphContext so that PrecompNode (in graph_nodes) can build +
/// compile nested scenes without directly linking graph_builder or graph_compiler.
/// The node catalog is set separately via ctx.resources.node_catalog.
void wire_precomp_build_factory(RenderGraphContext& ctx);

} // namespace chronon3d::graph
