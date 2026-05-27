#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Calculate the centered transform for a layer, considering the render context.
inline Transform calculate_centered_transform(const Transform& t, const RenderGraphContext&) {
    return t;
}

/// Determine if a layer should be rendered in centered mode.
inline bool should_use_centered_rendering(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    return ctx.modular_coordinates || (item.layer && item.layer->is_3d);
}

/// Determine if a layer has a custom transform beyond the default centering translation.
inline bool has_custom_transform(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!item.layer) return item.transform.any();

    const Vec3 p = item.transform.position;
    const Vec3 expected_center{ctx.width * 0.5f, ctx.height * 0.5f, 0.0f};

    const bool only_default_center =
        std::abs(p.x - expected_center.x) < 0.001f &&
        std::abs(p.y - expected_center.y) < 0.001f &&
        std::abs(p.z - expected_center.z) < 0.001f &&
        item.transform.rotation.w == 1.0f &&
        item.transform.scale == Vec3{1.0f, 1.0f, 1.0f} &&
        item.transform.anchor == Vec3{0.0f, 0.0f, 0.0f} &&
        item.transform.opacity == 1.0f;

    return item.transform.any() && !only_default_center;
}

inline bool layer_needs_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!item.layer) return false;

    return item.projected
        || item.layer->kind == LayerKind::Precomp
        || item.layer->kind == LayerKind::Video
        || has_custom_transform(item, ctx);
}

} // namespace chronon3d::graph::detail
