#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <span>

namespace chronon3d {
    struct Layer;
}

namespace chronon3d::graph {

/// Metadata for a projected layer that acts as a shadow caster.
struct ShadowCasterInfo {
    const chronon3d::Layer* layer{nullptr};
    Mat4  world_matrix{1.0f};
    Mat4  projection_matrix{1.0f};
    float world_z{0.0f};
    bool  projected{false};
};

}  // namespace chronon3d::graph

namespace chronon3d::graph::detail {

/// Append a lighting node for projected 2.5D layers with lit materials.
void append_lighting_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                    const LayerGraphItem& item,
                                    const RenderGraphContext& ctx);

/// Append shadow passes from casters onto the receiver layer (V1 trade-off).
void append_shadow_passes_if_needed(
    RenderGraph& graph,
    GraphNodeId& receiver_output,
    const LayerGraphItem& receiver_item,
    std::span<const ShadowCasterInfo> casters,
    const RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
