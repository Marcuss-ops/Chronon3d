#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Calculate the centered transform for a layer, considering the render context.
inline Transform calculate_centered_transform(const Transform& t, const RenderGraphContext& ctx) {
    Transform centered = t;
    centered.position.x -= ctx.width  * 0.5f;
    centered.position.y -= ctx.height * 0.5f;
    return centered;
}

/// Determine if a layer should be rendered in centered mode.
inline bool should_use_centered_rendering(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    const bool has_layer_pos = item.transform.position.x != 0.0f
                            || item.transform.position.y != 0.0f;
    return !item.projected
        && has_layer_pos
        && item.layer->kind == LayerKind::Normal
        && !ctx.modular_coordinates;
}

} // namespace chronon3d::graph::detail
