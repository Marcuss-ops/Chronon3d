// ---------------------------------------------------------------------------
// overlay_spatial_panels.cpp — Top-down XZ preview + Side-view depth map
// Extracted from camera_debug_overlay.cpp
// ---------------------------------------------------------------------------

#include "camera_debug_overlay_panels.hpp"
#include <algorithm>

namespace chronon3d {

void draw_topdown_preview(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    if (!ctx.options.show_topdown_preview) return;

    float td_w = 280.0f, td_h = 220.0f, td_margin = 20.0f, panel_gap = 10.0f;
    float td_x, td_y;
    switch (ctx.options.anchor) {
        case OverlayAnchor::BottomRight: td_x = td_margin; td_y = td_margin; break;
        case OverlayAnchor::BottomLeft: td_x = ctx.viewport.width - td_w - td_margin; td_y = td_margin; break;
        case OverlayAnchor::TopRight: td_x = td_margin; td_y = ctx.viewport.height - td_h - td_margin; break;
        case OverlayAnchor::TopLeft: default: td_x = ctx.viewport.width - td_w - td_margin; td_y = ctx.viewport.height - td_h - td_margin; break;
    }
    td_x += ctx.options.panel_offset_x; td_y += ctx.options.panel_offset_y;

    l.rect("topdown_bg", RectParams{.size = {td_w, td_h}, .pos = {td_x, td_y, 0.0f}, .fill = FillStyle::solid(Color{0.0f, 0.02f, 0.05f, 0.7f}), .stroke = {.enabled = true, .color = Color{0.3f, 0.5f, 0.8f, 0.35f}, .width = 1.0f}});
    l.text("topdown_title", TextSpec{.content = {.value = "TOP-DOWN VIEW (XZ)"}, .placement = {TextPlacementKind::Absolute, {td_x + 10.0f, td_y + 16.0f}}, .font = {.font_size = 10.0f}, .appearance = {.color = Color{0.8f, 0.85f, 1.0f, 0.8f}}});

    float w_min_x = 1e9f, w_max_x = -1e9f, w_min_z = 1e9f, w_max_z = -1e9f;
    for (size_t pair_idx = 0; pair_idx < ctx.resolved.size(); ++pair_idx) {
        Vec3 pos(ctx.resolved.resolved(pair_idx).world_matrix[3]);
        w_min_x = std::min(w_min_x, pos.x); w_max_x = std::max(w_max_x, pos.x);
        w_min_z = std::min(w_min_z, pos.z); w_max_z = std::max(w_max_z, pos.z);
    }
    w_min_x = std::min(w_min_x, ctx.camera.position.x); w_max_x = std::max(w_max_x, ctx.camera.position.x);
    w_min_z = std::min(w_min_z, ctx.camera.position.z); w_max_z = std::max(w_max_z, ctx.camera.position.z);
    if (w_min_x >= w_max_x) { w_min_x -= 200.0f; w_max_x += 200.0f; }
    if (w_min_z >= w_max_z) { w_min_z -= 200.0f; w_max_z += 200.0f; }
    float range_x = w_max_x - w_min_x, range_z = w_max_z - w_min_z;
    float max_range = std::max(range_x, range_z), pad = max_range * 0.15f;
    w_min_x -= pad; w_max_x += pad; w_min_z -= pad; w_max_z += pad;
    range_x = w_max_x - w_min_x; range_z = w_max_z - w_min_z;

    float draw_margin = 22.0f, draw_w = td_w - 2.0f * draw_margin, draw_h = td_h - 36.0f, draw_y0 = td_y + 28.0f;
    float sx = (range_x > 1e-3f) ? draw_w / range_x : 1.0f;
    float sz = (range_z > 1e-3f) ? draw_h / range_z : 1.0f;
    float s = std::min(sx, sz);
    auto to_td = [&](float wx, float wz) -> Vec2 { return {td_x + draw_margin + (wx - w_min_x) * s, draw_y0 + draw_h - (wz - w_min_z) * s}; };

    l.line("td_grid_h", LineParams{.from = {td_x + draw_margin, draw_y0 + draw_h * 0.5f, 0.0f}, .to = {td_x + td_w - draw_margin, draw_y0 + draw_h * 0.5f, 0.0f}, .thickness = 0.5f, .color = Color{0.25f, 0.35f, 0.55f, 0.2f}});
    l.line("td_grid_v", LineParams{.from = {td_x + draw_margin + draw_w * 0.5f, draw_y0, 0.0f}, .to = {td_x + draw_margin + draw_w * 0.5f, draw_y0 + draw_h, 0.0f}, .thickness = 0.5f, .color = Color{0.25f, 0.35f, 0.55f, 0.2f}});
    l.text("td_axis_x", TextSpec{.content = {.value = "+X"}, .placement = {TextPlacementKind::Absolute, {td_x + td_w - draw_margin - 14.0f, draw_y0 + draw_h + 2.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.4f, 0.4f, 0.5f}}});
    l.text("td_axis_z", TextSpec{.content = {.value = "+Z"}, .placement = {TextPlacementKind::Absolute, {td_x + draw_margin - 2.0f, draw_y0 - 6.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.4f, 0.4f, 0.6f, 0.5f}}});

    int td_idx = 0;
    for (size_t pair_idx = 0; pair_idx < ctx.resolved.size(); ++pair_idx) {
        Vec3 pos(ctx.resolved.resolved(pair_idx).world_matrix[3]);
        const std::string& pair_name = ctx.resolved.name_at(pair_idx);
        Vec2 screen = to_td(pos.x, pos.z);
        if (screen.x < td_x + draw_margin || screen.x > td_x + td_w - draw_margin || screen.y < draw_y0 || screen.y > draw_y0 + draw_h) continue;

        float z_norm = (range_z > 1e-3f) ? (pos.z - w_min_z) / range_z : 0.5f;
        Color layer_color;
        if (pos.z < 0.0f) { layer_color = Color{0.2f + z_norm * 0.6f, 0.8f, 1.0f, 0.85f}; }
        else if (pos.z > 0.0f) { layer_color = Color{0.3f, 0.4f + z_norm * 0.3f, 0.9f - z_norm * 0.3f, 0.75f}; }
        else { layer_color = Color{0.2f, 0.9f, 0.2f, 0.85f}; }

        for (const auto& lr : ctx.report.layers) { if (lr.name == pair_name) { layer_color = lr.passed ? Color{0.2f, 0.9f, 0.2f, 0.85f} : Color{1.0f, 0.3f, 0.15f, 0.85f}; break; } }
        bool is_null = pair_name.find("_null") != std::string::npos || pair_name.find("_parent") != std::string::npos;
        if (is_null) layer_color = Color{0.0f, 0.9f, 1.0f, 0.8f};

        l.circle("td_layer_" + std::to_string(td_idx), CircleParams{.radius = is_null ? 4.0f : 3.5f, .color = layer_color, .pos = {screen.x, screen.y, 0.0f}});
        if (!is_null && td_idx < 8) { l.text("td_lbl_" + std::to_string(td_idx), TextSpec{.content = {.value = pair_name}, .placement = {TextPlacementKind::Absolute, {screen.x + 5.0f, screen.y - 4.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.7f, 0.75f, 0.9f, 0.55f}}}); }
        td_idx++;
    }

    Vec2 cam_screen = to_td(ctx.camera.position.x, ctx.camera.position.z);
    if (cam_screen.x >= td_x + draw_margin && cam_screen.x <= td_x + td_w - draw_margin && cam_screen.y >= draw_y0 && cam_screen.y <= draw_y0 + draw_h) {
        l.circle("td_cam_pos", CircleParams{.radius = 5.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.95f}, .pos = {cam_screen.x, cam_screen.y, 0.0f}});
        float yaw_rad = glm::radians(ctx.camera.rotation.y);
        float dir_x = std::sin(yaw_rad), dir_z = -std::cos(yaw_rad);
        l.line("td_cam_dir", LineParams{.from = {cam_screen.x, cam_screen.y, 0.0f}, .to = {cam_screen.x + dir_x * 40.0f, cam_screen.y - dir_z * 40.0f, 0.0f}, .thickness = 2.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.8f}});
        float fov_half = glm::radians(ctx.camera.fov_deg * 0.5f);
        l.line("td_fov_l", LineParams{.from = {cam_screen.x, cam_screen.y, 0.0f}, .to = {cam_screen.x + std::sin(yaw_rad - fov_half) * 30.0f, cam_screen.y + std::cos(yaw_rad - fov_half) * 30.0f, 0.0f}, .thickness = 1.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.35f}});
        l.line("td_fov_r", LineParams{.from = {cam_screen.x, cam_screen.y, 0.0f}, .to = {cam_screen.x + std::sin(yaw_rad + fov_half) * 30.0f, cam_screen.y + std::cos(yaw_rad + fov_half) * 30.0f, 0.0f}, .thickness = 1.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.35f}});
        l.text("td_cam_lbl", TextSpec{.content = {.value = "CAM"}, .placement = {TextPlacementKind::Absolute, {cam_screen.x + 7.0f, cam_screen.y - 8.0f}}, .font = {.font_size = 8.0f}, .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 0.8f}}});
    }

    l.circle("td_leg_near", CircleParams{.radius = 3.0f, .color = Color{0.4f, 0.9f, 1.0f, 0.85f}, .pos = {td_x + 10.0f, td_y + td_h - 8.0f, 0.0f}});
    l.text("td_leg_near_t", TextSpec{.content = {.value = "near"}, .placement = {TextPlacementKind::Absolute, {td_x + 16.0f, td_y + td_h - 12.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.6f, 0.7f, 0.5f}}});
    l.circle("td_leg_far", CircleParams{.radius = 3.0f, .color = Color{0.3f, 0.5f, 0.9f, 0.75f}, .pos = {td_x + 52.0f, td_y + td_h - 8.0f, 0.0f}});
    l.text("td_leg_far_t", TextSpec{.content = {.value = "far"}, .placement = {TextPlacementKind::Absolute, {td_x + 58.0f, td_y + td_h - 12.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.6f, 0.7f, 0.5f}}});
    l.circle("td_leg_cam", CircleParams{.radius = 3.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.9f}, .pos = {td_x + 88.0f, td_y + td_h - 8.0f, 0.0f}});
    l.text("td_leg_cam_t", TextSpec{.content = {.value = "cam"}, .placement = {TextPlacementKind::Absolute, {td_x + 94.0f, td_y + td_h - 12.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.6f, 0.7f, 0.5f}}});
}

void draw_sideview_depth(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    if (!ctx.options.show_depth_side_view) return;

    float sv_w = 280.0f, sv_h = 220.0f, sv_margin = 20.0f;
    float sv_x, sv_y;
    switch (ctx.options.anchor) {
        case OverlayAnchor::BottomRight: sv_x = sv_margin + 290.0f; sv_y = sv_margin; break;
        case OverlayAnchor::BottomLeft: sv_x = ctx.viewport.width - 2.0f * sv_w - 2.0f * sv_margin - 10.0f; sv_y = sv_margin; break;
        case OverlayAnchor::TopRight: sv_x = sv_margin + 290.0f; sv_y = ctx.viewport.height - sv_h - sv_margin; break;
        case OverlayAnchor::TopLeft: default: sv_x = ctx.viewport.width - 2.0f * sv_w - 2.0f * sv_margin - 10.0f; sv_y = ctx.viewport.height - sv_h - sv_margin; break;
    }
    sv_x += ctx.options.panel_offset_x; sv_y += ctx.options.panel_offset_y;

    l.rect("sideview_bg", RectParams{.size = {sv_w, sv_h}, .pos = {sv_x, sv_y, 0.0f}, .fill = FillStyle::solid(Color{0.0f, 0.03f, 0.02f, 0.7f}), .stroke = {.enabled = true, .color = Color{0.3f, 0.6f, 0.5f, 0.35f}, .width = 1.0f}});
    l.text("sv_title", TextSpec{.content = {.value = "DEPTH SIDE VIEW (X vs Z)"}, .placement = {TextPlacementKind::Absolute, {sv_x + 10.0f, sv_y + 16.0f}}, .font = {.font_size = 10.0f}, .appearance = {.color = Color{0.8f, 1.0f, 0.85f, 0.8f}}});

    float w_min_x = 1e9f, w_max_x = -1e9f, w_min_z = 1e9f, w_max_z = -1e9f;
    for (size_t pair_idx = 0; pair_idx < ctx.resolved.size(); ++pair_idx) {
        Vec3 pos(ctx.resolved.resolved(pair_idx).world_matrix[3]);
        w_min_x = std::min(w_min_x, pos.x); w_max_x = std::max(w_max_x, pos.x);
        w_min_z = std::min(w_min_z, pos.z); w_max_z = std::max(w_max_z, pos.z);
    }
    w_min_x = std::min(w_min_x, ctx.camera.position.x); w_max_x = std::max(w_max_x, ctx.camera.position.x);
    w_min_z = std::min(w_min_z, ctx.camera.position.z); w_max_z = std::max(w_max_z, ctx.camera.position.z);
    if (w_min_x >= w_max_x) { w_min_x -= 200.0f; w_max_x += 200.0f; }
    if (w_min_z >= w_max_z) { w_min_z -= 200.0f; w_max_z += 200.0f; }
    float range_x = w_max_x - w_min_x, range_z = w_max_z - w_min_z;
    float pad_x = range_x * 0.15f, pad_z = range_z * 0.15f;
    w_min_x -= pad_x; w_max_x += pad_x; w_min_z -= pad_z; w_max_z += pad_z;
    range_x = w_max_x - w_min_x; range_z = w_max_z - w_min_z;

    float d_margin = 22.0f, d_w = sv_w - 2.0f * d_margin, d_h = sv_h - 36.0f, d_y0 = sv_y + 28.0f;
    float sx = (range_x > 1e-3f) ? d_w / range_x : 1.0f;
    float sz = (range_z > 1e-3f) ? d_h / range_z : 1.0f;
    float s = std::min(sx, sz);
    auto to_sv = [&](float wx, float wz) -> Vec2 { return {sv_x + d_margin + (wx - w_min_x) * s, d_y0 + d_h - (wz - w_min_z) * s}; };

    l.line("sv_grid_h", LineParams{.from = {sv_x + d_margin, d_y0 + d_h * 0.5f, 0.0f}, .to = {sv_x + sv_w - d_margin, d_y0 + d_h * 0.5f, 0.0f}, .thickness = 0.5f, .color = Color{0.25f, 0.45f, 0.35f, 0.2f}});
    l.line("sv_grid_v", LineParams{.from = {sv_x + d_margin + d_w * 0.5f, d_y0, 0.0f}, .to = {sv_x + d_margin + d_w * 0.5f, d_y0 + d_h, 0.0f}, .thickness = 0.5f, .color = Color{0.25f, 0.45f, 0.35f, 0.2f}});

    if (w_min_z < 0.0f && w_max_z > 0.0f) {
        float z0_y = d_y0 + d_h - (0.0f - w_min_z) * s;
        l.line("sv_z0", LineParams{.from = {sv_x + d_margin, z0_y, 0.0f}, .to = {sv_x + sv_w - d_margin, z0_y, 0.0f}, .thickness = 1.0f, .color = Color{0.5f, 0.8f, 0.5f, 0.3f}});
        l.text("sv_z0_lbl", TextSpec{.content = {.value = "Z=0"}, .placement = {TextPlacementKind::Absolute, {sv_x + sv_w - d_margin + 2.0f, z0_y - 4.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.5f, 0.8f, 0.5f, 0.5f}}});
    }

    l.text("sv_axis_x", TextSpec{.content = {.value = "+X"}, .placement = {TextPlacementKind::Absolute, {sv_x + sv_w - d_margin - 14.0f, d_y0 + d_h + 2.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.5f, 0.4f, 0.5f}}});
    l.text("sv_axis_z", TextSpec{.content = {.value = "+Z"}, .placement = {TextPlacementKind::Absolute, {sv_x + d_margin - 2.0f, d_y0 - 6.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.4f, 0.6f, 0.4f, 0.5f}}});

    int sv_idx = 0;
    for (size_t pair_idx = 0; pair_idx < ctx.resolved.size(); ++pair_idx) {
        Vec3 pos(ctx.resolved.resolved(pair_idx).world_matrix[3]);
        const std::string& pair_name = ctx.resolved.name_at(pair_idx);
        Vec2 screen = to_sv(pos.x, pos.z);
        if (screen.x < sv_x + d_margin || screen.x > sv_x + sv_w - d_margin || screen.y < d_y0 || screen.y > d_y0 + d_h) continue;

        bool in_report = false, passed = true;
        for (const auto& lr : ctx.report.layers) { if (lr.name == pair_name) { in_report = true; passed = lr.passed; break; } }
        bool is_null = pair_name.find("_null") != std::string::npos || pair_name.find("_parent") != std::string::npos;
        Color bar_color;
        if (is_null) { bar_color = Color{0.0f, 0.9f, 1.0f, 0.7f}; }
        else if (in_report) { bar_color = passed ? Color{0.2f, 0.9f, 0.3f, 0.85f} : Color{1.0f, 0.3f, 0.15f, 0.85f}; }
        else { bar_color = pos.z < 0.0f ? Color{0.3f, 0.85f, 1.0f, 0.8f} : Color{0.3f, 0.5f, 0.9f, 0.7f}; }

        l.rect("sv_bar_" + std::to_string(sv_idx), RectParams{.size = {16.0f, 5.0f}, .pos = {screen.x - 8.0f, screen.y - 2.5f, 0.0f}, .fill = FillStyle::solid(bar_color), .stroke = {.enabled = true, .color = Color{bar_color.r, bar_color.g, bar_color.b, 0.5f}, .width = 0.5f}});

        if (!is_null && w_min_z < 0.0f && w_max_z > 0.0f) {
            float z0_y = d_y0 + d_h - (0.0f - w_min_z) * s;
            if (std::abs(screen.y - z0_y) > 3.0f) {
                l.line("sv_drop_" + std::to_string(sv_idx), LineParams{.from = {screen.x, screen.y, 0.0f}, .to = {screen.x, z0_y, 0.0f}, .thickness = 0.5f, .color = Color{bar_color.r, bar_color.g, bar_color.b, 0.25f}});
            }
        }

        if (!is_null && sv_idx < 6) { l.text("sv_lbl_" + std::to_string(sv_idx), TextSpec{.content = {.value = pair_name}, .placement = {TextPlacementKind::Absolute, {screen.x + 10.0f, screen.y - 3.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.7f, 0.9f, 0.8f, 0.55f}}}); }
        sv_idx++;
    }

    Vec2 cam_sv = to_sv(ctx.camera.position.x, ctx.camera.position.z);
    if (cam_sv.x >= sv_x + d_margin && cam_sv.x <= sv_x + sv_w - d_margin && cam_sv.y >= d_y0 && cam_sv.y <= d_y0 + d_h) {
        l.circle("sv_cam", CircleParams{.radius = 5.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.9f}, .pos = {cam_sv.x, cam_sv.y, 0.0f}});
        l.text("sv_cam_lbl", TextSpec{.content = {.value = "CAM"}, .placement = {TextPlacementKind::Absolute, {cam_sv.x + 7.0f, cam_sv.y - 4.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 0.8f}}});
    }

    l.circle("sv_leg_pass", CircleParams{.radius = 3.0f, .color = Color{0.2f, 0.9f, 0.3f, 0.85f}, .pos = {sv_x + 10.0f, sv_y + sv_h - 8.0f, 0.0f}});
    l.text("sv_leg_pass_t", TextSpec{.content = {.value = "pass"}, .placement = {TextPlacementKind::Absolute, {sv_x + 16.0f, sv_y + sv_h - 12.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.7f, 0.65f, 0.5f}}});
    l.circle("sv_leg_fail", CircleParams{.radius = 3.0f, .color = Color{1.0f, 0.3f, 0.15f, 0.85f}, .pos = {sv_x + 50.0f, sv_y + sv_h - 8.0f, 0.0f}});
    l.text("sv_leg_fail_t", TextSpec{.content = {.value = "fail"}, .placement = {TextPlacementKind::Absolute, {sv_x + 56.0f, sv_y + sv_h - 12.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.7f, 0.6f, 0.6f, 0.5f}}});
    l.circle("sv_leg_cam", CircleParams{.radius = 3.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.9f}, .pos = {sv_x + 88.0f, sv_y + sv_h - 8.0f, 0.0f}});
    l.text("sv_leg_cam_t", TextSpec{.content = {.value = "cam"}, .placement = {TextPlacementKind::Absolute, {sv_x + 94.0f, sv_y + sv_h - 12.0f}}, .font = {.font_size = 7.0f}, .appearance = {.color = Color{0.6f, 0.7f, 0.65f, 0.5f}}});
}

} // namespace chronon3d
