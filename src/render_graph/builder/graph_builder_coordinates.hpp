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
    if (item.projected) return false;
    return ctx.modular_coordinates || item.layer->is_3d;
}

} // namespace chronon3d::graph::detail
