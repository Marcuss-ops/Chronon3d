// ---------------------------------------------------------------------------
// diagnostics/nulls_overlay.cpp
// ---------------------------------------------------------------------------

#include "nulls_overlay.hpp"
#include "internal/helpers.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::renderer::diagnostics {

void draw_null_overlay(Framebuffer& fb, const Scene& scene, const Camera2_5D& camera) {
    const f32 w = static_cast<f32>(fb.width());
    const f32 h = static_cast<f32>(fb.height());

    auto get_screen_pos = [&](const Vec3& world_pos) -> Vec2 {
        if (!camera.enabled) {
            return Vec2(world_pos.x, world_pos.y);
        }
        // Single projection authority: the renderer contract owns optics mode,
        // focal, lens shift and visibility. The world-xy fallback on invisible
        // points preserves the previous overlay behaviour.
        const auto projected = camera_math::project_world_point(
            camera, world_pos, camera_math::Viewport2D{w, h});
        if (!projected.visible) {
            return Vec2(world_pos.x, world_pos.y);
        }
        return Vec2(projected.screen.x, projected.screen.y);
    };

    // Draw parent-child connections
    for (const auto& layer : scene.layers()) {
        if (layer.parent_name.empty()) continue;
        for (const auto& parent_layer : scene.layers()) {
            if (parent_layer.name == layer.parent_name) {
                Vec2 child_screen = get_screen_pos(layer.transform.position);
                Vec2 parent_screen = get_screen_pos(parent_layer.transform.position);
                renderer::bline(fb, parent_screen, child_screen, Color{1.0f, 0.5f, 0.0f, 0.7f});
                break;
            }
        }
    }

    // Draw null layer crosshairs and axes
    for (const auto& layer : scene.layers()) {
        if (layer.kind != LayerKind::Null) continue;
        Vec2 pos_screen = get_screen_pos(layer.transform.position);
        internal::draw_crosshair(fb, pos_screen, 8.0f, Color{1.0f, 0.0f, 1.0f, 0.9f});

        Vec3 x_axis = layer.transform.position + layer.transform.rotation * Vec3(30.0f, 0.0f, 0.0f);
        Vec3 y_axis = layer.transform.position + layer.transform.rotation * Vec3(0.0f, 30.0f, 0.0f);
        Vec2 x_screen = get_screen_pos(x_axis);
        Vec2 y_screen = get_screen_pos(y_axis);

        renderer::bline(fb, pos_screen, x_screen, Color{1.0f, 0.0f, 0.0f, 0.9f});
        renderer::bline(fb, pos_screen, y_screen, Color{0.0f, 1.0f, 0.0f, 0.9f});
    }
}

} // namespace chronon3d::renderer::diagnostics
