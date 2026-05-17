#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/math/mat4.hpp>
#include <span>

namespace chronon3d::graph::detail {

// Metadata for a projected layer that casts shadows.
// Built in graph_builder_pipeline.cpp before the layer loop.
struct ShadowCasterInfo {
    const Layer*  layer{nullptr};
    Mat4          world_matrix{1.0f};
    Mat4          projection_matrix{1.0f};
    f32           world_z{0.0f};
    bool          projected{false};
};

// For each caster that matches receiver gating conditions, builds a
// caster source+transform sub-pipeline on-demand, creates a ShadowNode,
// and composites the result onto receiver_output.
void append_shadow_passes_if_needed(
    RenderGraph& graph,
    GraphNodeId& receiver_output,
    const LayerGraphItem& receiver_item,
    std::span<const ShadowCasterInfo> casters,
    const RenderGraphContext& ctx
);

} // namespace chronon3d::graph::detail
