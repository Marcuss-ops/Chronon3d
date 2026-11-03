#pragma once

#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>

#include <cstdint>

namespace chronon3d::graph::detail {

/// Attach the runtime-prepared mesh source to a copied RenderNode. The
/// authoring node remains logical-only; filesystem work happens through the
/// already-wired resolver/cache boundary.
[[nodiscard]] ::chronon3d::RenderNode materialize_mesh_node(
    const ::chronon3d::RenderNode& node,
    const RenderGraphContext& ctx);

/// The frame's inline-mesh animation payload for a compiled layer, or null
/// when the layer has no animation sample attached to this frame. The fresh
/// build path reads it so authored animation reaches the graph without a
/// refresh pass; the refresh path reads it for the same reason.
[[nodiscard]] const ::chronon3d::compiler::SampledMeshParameters* sampled_mesh_for_layer(
    const RenderGraphContext& ctx, std::uint32_t layer_index) noexcept;

/// Replace a mesh node's inline geometry with the frame-accurate interpolation
/// of its animation samples. Returns the node unchanged when it carries no
/// mesh or the sample does not describe one; throws when the sample does not
/// match the authored topology, so a corrupt animation fails closed instead of
/// silently drawing the base pose.
[[nodiscard]] ::chronon3d::RenderNode deform_inline_mesh_node(
    const ::chronon3d::RenderNode& node,
    const ::chronon3d::compiler::SampledMeshParameters& sampled);

/// Create the source sub-graph for a single layer (Normal / Precomp / Video).
GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx,
                               const BuilderContext& node_ctx = {});

} // namespace chronon3d::graph::detail
