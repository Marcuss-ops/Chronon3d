// ---------------------------------------------------------------------------
// overlay_hud_panels.cpp — Safe area, target crosshair, bounds, null/parent markers
// Extracted from camera_debug_overlay.cpp
// ---------------------------------------------------------------------------

#include "camera_debug_overlay_panels.hpp"
#include <algorithm>

namespace chronon3d {

void draw_safe_area_and_target(const OverlayContext& ctx, bool& has_target, ScreenPoint& sp, Color& target_color) {
    auto& l = ctx.layer;

    bool safe_area_failed = false;
    for (const auto& f : ctx.report.failures) {
        if (f.find("safe area") != std::string::npos || f.find("outside safe") != std::string::npos) {
            safe_area_failed = true;
            break;
        }
    }
    Color safe_area_color = safe_area_failed ? Color{1.0f, 0.2f, 0.2f, 0.7f} : Color{0.2f, 0.9f, 0.2f, 0.5f};

    if (ctx.options.show_safe_area) {
        l.rect("safe_area_hud", RectParams{
            .size = {ctx.viewport.width * 0.90f, ctx.viewport.height * 0.90f},
            .pos = {ctx.viewport.width * 0.05f, ctx.viewport.height * 0.05f, 0.0f},
            .fill = Fill{ .enabled = false },
            .stroke = { .enabled = true, .color = safe_area_color, .width = 1.5f }
        });
    }

    Vec3 target_world{0.0f, 0.0f, 0.0f};
    has_target = false;
    if (!ctx.camera.camera.target_name.empty()) {
        auto pos_opt = ctx.resolved.world_position(std::string(ctx.camera.camera.target_name));
        if (pos_opt) {
            target_world = *pos_opt;
            has_target = true;
        }
    } else if (ctx.camera.camera.point_of_interest_enabled) {
        target_world = ctx.camera.camera.point_of_interest;
        has_target = true;
    }

    if (has_target) {
        sp = project_world_to_screen(target_world, ctx.camera.camera.camera, ctx.viewport);
    }

    target_color = Color{0.2f, 0.9f, 0.2f, 0.8f};
    if (ctx.report.target_center_error_px > 3.0f) {
        target_color = Color{1.0f, 0.2f, 0.2f, 0.8f};
    } else if (ctx.report.target_center_error_px > 1.5f) {
        target_color = Color{1.0f, 0.8f, 0.0f, 0.8f};
    }

    if (ctx.options.show_target) {
        Vec2 center = has_target && !sp.behind_camera ? sp.position : Vec2{ctx.viewport.width * 0.5f, ctx.viewport.height * 0.5f};
        l.line("target_cross_h", LineParams{
            .from = {center.x - 25.0f, center.y, 0.0f},
            .to = {center.x + 25.0f, center.y, 0.0f},
            .thickness = 2.0f, .color = target_color
        });
        l.line("target_cross_v", LineParams{
            .from = {center.x, center.y - 25.0f, 0.0f},
            .to = {center.x, center.y + 25.0f, 0.0f},
            .thickness = 2.0f, .color = target_color
        });
        l.circle("target_cross_dot", CircleParams{
            .radius = 4.0f, .color = target_color,
            .pos = {center.x, center.y, 0.0f}
        });
    }
}

void draw_camera_to_target_line(const OverlayContext& ctx, const ScreenPoint& sp, bool has_target, const Color& target_color) {
    auto& l = ctx.layer;
    if (!ctx.options.show_camera_to_target_line || !has_target || sp.behind_camera) return;

    l.line("camera_to_target_line", LineParams{
        .from = {ctx.viewport.width * 0.5f, ctx.viewport.height * 0.5f, 0.0f},
        .to = {sp.position.x, sp.position.y, 0.0f},
        .thickness = 1.5f, .color = target_color
    });

    float dist = glm::distance(sp.position, Vec2{ctx.viewport.width * 0.5f, ctx.viewport.height * 0.5f});
    if (dist > 1.0f) {
        l.text("target_deviation_text", TextParams{
            .text = std::to_string(static_cast<int>(dist)) + "px error",
            .pos = {(ctx.viewport.width * 0.5f + sp.position.x) * 0.5f + 10.0f,
                    (ctx.viewport.height * 0.5f + sp.position.y) * 0.5f + 10.0f, 0.0f},
            .font_size = 12.0f, .color = target_color
        });
    }
}

void draw_null_parent_markers(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    for (const auto& pair : ctx.resolved.resolved) {
        const std::string& name = pair.first;
        if (name.find("_null") != std::string::npos || name.find("_parent") != std::string::npos) {
            Vec3 null_pos = Vec3(pair.second.world_matrix[3]);
            ScreenPoint n_sp = project_world_to_screen(null_pos, ctx.camera.camera.camera, ctx.viewport);
            if (!n_sp.behind_camera) {
                l.rect("null_parent_rect_" + name, RectParams{
                    .size = {12.0f, 12.0f},
                    .pos = {n_sp.position.x - 6.0f, n_sp.position.y - 6.0f, 0.0f},
                    .fill = Fill{ .enabled = false },
                    .stroke = { .enabled = true, .color = Color{0.0f, 0.9f, 1.0f, 0.8f}, .width = 1.5f }
                });
                l.line("null_parent_h_" + name, LineParams{
                    .from = {n_sp.position.x - 12.0f, n_sp.position.y, 0.0f},
                    .to = {n_sp.position.x + 12.0f, n_sp.position.y, 0.0f},
                    .thickness = 1.0f, .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                });
                l.line("null_parent_v_" + name, LineParams{
                    .from = {n_sp.position.x, n_sp.position.y - 12.0f, 0.0f},
                    .to = {n_sp.position.x, n_sp.position.y + 12.0f, 0.0f},
                    .thickness = 1.0f, .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                });
                l.text("null_parent_lbl_" + name, TextParams{
                    .text = name,
                    .pos = {n_sp.position.x + 15.0f, n_sp.position.y + 5.0f, 0.0f},
                    .font_size = 10.0f, .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                });
            }
        }
    }
}

void draw_parent_child_links(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    for (const auto& pair : ctx.resolved.resolved) {
        const std::string& child_name = pair.first;
        const ResolvedTransform3D& r3d = pair.second;
        if (!r3d.local.parent_name.empty()) {
            auto parent_pos_opt = ctx.resolved.world_position(r3d.local.parent_name);
            auto child_pos_opt = ctx.resolved.world_position(child_name);
            if (parent_pos_opt && child_pos_opt) {
                ScreenPoint parent_sp = project_world_to_screen(*parent_pos_opt, ctx.camera.camera.camera, ctx.viewport);
                ScreenPoint child_sp = project_world_to_screen(*child_pos_opt, ctx.camera.camera.camera, ctx.viewport);
                if (!parent_sp.behind_camera && !child_sp.behind_camera) {
                    l.line("link_" + r3d.local.parent_name + "_" + child_name, LineParams{
                        .from = {parent_sp.position.x, parent_sp.position.y, 0.0f},
                        .to = {child_sp.position.x, child_sp.position.y, 0.0f},
                        .thickness = 1.0f, .color = Color{0.0f, 0.7f, 1.0f, 0.4f}
                    });
                }
            }
        }
    }
}

void draw_projected_bounds(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    if (!ctx.options.show_projected_bounds) return;

    int idx = 0;
    for (const auto& lr : ctx.report.layers) {
        Color border_color = lr.passed ? Color{0.2f, 0.9f, 0.2f, 0.6f} : Color{1.0f, 0.2f, 0.2f, 0.8f};
        if (ctx.report.composition_name == "CameraFrustumCullingPrecisionTest") {
            border_color = lr.bounds.visible_ratio > 0.0f
                ? Color{0.2f, 0.9f, 0.2f, 0.6f}
                : Color{0.5f, 0.5f, 0.5f, 0.15f};
        }

        Vec2 b_size = lr.bounds.max - lr.bounds.min;
        if (b_size.x > 0.0f && b_size.y > 0.0f) {
            std::string node_name = "bounds_hud_" + lr.name + "_" + std::to_string(idx);
            l.rect(node_name, RectParams{
                .size = b_size, .pos = {lr.bounds.min.x, lr.bounds.min.y, 0.0f},
                .fill = Fill{ .enabled = false },
                .stroke = { .enabled = true, .color = border_color, .width = 1.5f }
            });
            if (ctx.options.show_layer_names) {
                l.text("label_hud_" + lr.name + "_" + std::to_string(idx), TextParams{
                    .text = lr.name + (lr.passed ? " (PASS)" : " (FAIL)"),
                    .pos = {lr.bounds.min.x + 5.0f, lr.bounds.min.y + 15.0f, 0.0f},
                    .font_size = 12.0f, .color = border_color
                });
            }
        }
        idx++;
    }
}

} // namespace chronon3d
