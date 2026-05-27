#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace chronon3d {
    struct Layer;
}

namespace chronon3d::graph::detail {

/// Blend the layer output into the current frame buffer using the layer's blend mode.
void append_composite_pass(RenderGraph& graph, GraphNodeId& current,
                           GraphNodeId layer_output, const chronon3d::Layer& layer,
                           bool is_static, const RenderGraphContext& ctx);

/// Append effect stack nodes for layer effects and optionally DOF blur.
void append_effect_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                  const chronon3d::Layer& layer, const LayerGraphItem& item,
                                  const Camera2_5DRuntime& cam25d);

/// Append a mask node if the layer has an active mask.
void append_mask_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                const LayerGraphItem& item,
                                const RenderGraphContext& ctx);

/// Append a transform node if the layer needs render-space transformation.
void append_transform_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                     const LayerGraphItem& item, const RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
