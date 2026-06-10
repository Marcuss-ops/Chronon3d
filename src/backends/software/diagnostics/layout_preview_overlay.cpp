// ---------------------------------------------------------------------------
// diagnostics/layout_preview_overlay.cpp
// ---------------------------------------------------------------------------

#include "layout_preview_overlay.hpp"
#include "bbox_overlay.hpp"
#include "internal/helpers.hpp"
#include <chronon3d/backends/software/shape_processor.hpp>
#include <spdlog/spdlog.h>
#include <cmath>

namespace chronon3d::renderer::diagnostics {

void draw_layout_preview(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                         renderer::ShapeProcessor& processor) {
    const auto bbox = processor.compute_world_bbox(node.shape, state.matrix, 0.0f);
    draw_bbox_overlay(fb, bbox, Color{0.15f, 0.90f, 1.0f, 0.95f});

    Vec4 anchor = state.matrix * Vec4(node.world_transform.anchor, 1.0f);
    if (std::abs(anchor.w) > 1e-7f) {
        internal::draw_crosshair(fb, {anchor.x / anchor.w, anchor.y / anchor.w}, 6.0f,
                                 Color{1.0f, 0.20f, 0.72f, 0.95f});
    }

    internal::draw_crosshair(
        fb,
        {static_cast<f32>(fb.width()) * 0.5f, static_cast<f32>(fb.height()) * 0.5f},
        16.0f,
        Color{1.0f, 1.0f, 1.0f, 0.55f}
    );

    const Vec4 world_pos = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Vec2 anchor_screen{0.0f, 0.0f};
    if (std::abs(anchor.w) > 1e-7f) {
        anchor_screen = {anchor.x / anchor.w, anchor.y / anchor.w};
    }

    spdlog::info(
        "[layout-preview] node='{}' type={} pos=({:.1f},{:.1f},{:.1f}) anchor=({:.1f},{:.1f},{:.1f}) bbox=[{},{},{},{}] screen_anchor=({:.1f},{:.1f})",
        node.name,
        internal::shape_type_name(node.shape.type),
        world_pos.x,
        world_pos.y,
        world_pos.z,
        node.world_transform.anchor.x,
        node.world_transform.anchor.y,
        node.world_transform.anchor.z,
        bbox.x0,
        bbox.y0,
        bbox.x1,
        bbox.y1,
        anchor_screen.x,
        anchor_screen.y
    );
}

} // namespace chronon3d::renderer::diagnostics
