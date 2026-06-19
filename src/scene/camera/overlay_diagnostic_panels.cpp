// ---------------------------------------------------------------------------
// overlay_diagnostic_panels.cpp — Standardised CameraDiagnosticOverlay
//
// Draws a consistent diagnostic HUD inside the same LayerBuilder as the
// other overlay panels:
//   1. Screen-centre crosshair (white, thin)
//   2. Target marker (green < 1.5 px, yellow < 3 px, red > 3 px error)
//   3. RGB camera axes projected from screen centre
//   4. Layer projected bounding boxes (from validator report)
//   5. Metrics text panel (composition, frame, camera pos, quat norm, …)
// ---------------------------------------------------------------------------

#include "camera_debug_overlay_panels.hpp"
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace chronon3d {

void draw_diagnostic_overlay(
    const OverlayContext& ctx,
    const CameraDiagnosticOverlay& diag
) {
    auto& l = ctx.layer;
    const Vec2 center{ctx.viewport.width * 0.5f, ctx.viewport.height * 0.5f};

    // ── 1. Screen-centre crosshair ─────────────────────────────────────
    if (diag.show_center_cross) {
        const Color cc{1.0f, 1.0f, 1.0f, 0.25f};
        l.line("diag_center_h", LineParams{
            .from = {center.x - 18.0f, center.y, 0.0f},
            .to   = {center.x + 18.0f, center.y, 0.0f},
            .thickness = 1.0f, .color = cc
        });
        l.line("diag_center_v", LineParams{
            .from = {center.x, center.y - 18.0f, 0.0f},
            .to   = {center.x, center.y + 18.0f, 0.0f},
            .thickness = 1.0f, .color = cc
        });
        l.circle("diag_center_dot", CircleParams{
            .radius = 3.0f,
            .color = cc,
            .pos   = {center.x, center.y, 0.0f}
        });
        l.text("diag_center_lbl", TextSpec{.content = {.value = "(SCREEN)"}, .font = {.font_size = 8.0f}, .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 0.15f}}, .position = {center.x + 20.0f, center.y + 4.0f, 0.0f}});
    }

    // ── 2. Target marker (coloured by deviation) ──────────────────────
    if (diag.show_target_marker) {
        Vec3 target_world{0.0f, 0.0f, 0.0f};
        bool has_target = false;

        if (!ctx.camera.target_name.empty()) {
            if (auto pos = ctx.resolved.world_position(std::string(ctx.camera.target_name))) {
                target_world = *pos;
                has_target = true;
            }
        } else if (ctx.camera.point_of_interest_enabled) {
            target_world = ctx.camera.point_of_interest;
            has_target = true;
        }

        if (has_target) {
            ScreenPoint sp = project_world_to_screen(target_world, ctx.camera, ctx.viewport);
            if (!sp.behind_camera) {
                float err = glm::distance(sp.position, center);

                Color tc{0.2f, 0.9f, 0.2f, 0.85f};
                if (err > 3.0f)      tc = Color{1.0f, 0.2f, 0.2f, 0.85f};
                else if (err > 1.5f) tc = Color{1.0f, 0.8f, 0.0f, 0.85f};

                // Cross at target
                l.line("diag_target_h", LineParams{
                    .from = {sp.position.x - 20.0f, sp.position.y, 0.0f},
                    .to   = {sp.position.x + 20.0f, sp.position.y, 0.0f},
                    .thickness = 2.0f, .color = tc
                });
                l.line("diag_target_v", LineParams{
                    .from = {sp.position.x, sp.position.y - 20.0f, 0.0f},
                    .to   = {sp.position.x, sp.position.y + 20.0f, 0.0f},
                    .thickness = 2.0f, .color = tc
                });
                l.circle("diag_target_ring", CircleParams{
                    .radius = 10.0f,
                    .pos    = {sp.position.x, sp.position.y, 0.0f},
                    .fill   = FillStyle{ .enabled = false },
                    .stroke = { .enabled = true, .color = tc, .width = 1.5f }
                });

                // Error text
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << err << " px";
                l.text("diag_target_err", TextSpec{.content = {.value = oss.str()}, .font = {.font_size = 10.0f}, .appearance = {.color = tc}, .position = {sp.position.x + 22.0f, sp.position.y + 6.0f, 0.0f}});
            }
        }
    }

    // ── 3. RGB camera axes (world X=R, Y=G, Z=B) ────────────────────
    if (diag.show_camera_axes) {
        CameraPose pose{ctx.camera.position, ctx.camera.rotation_quaternion()};
        const float axis_len = 50.0f;

        Vec3 origin_world = ctx.camera.position + pose.forward() * 10.0f;
        ScreenPoint sp_origin = project_world_to_screen(origin_world, ctx.camera, ctx.viewport);

        auto draw_axis = [&](const char* id, const Vec3& dir, const Color& col, const char* label) {
            Vec3 tip_world = origin_world + dir * axis_len;
            ScreenPoint sp_tip = project_world_to_screen(tip_world, ctx.camera, ctx.viewport);
            if (sp_origin.behind_camera || sp_tip.behind_camera) return;

            l.line(std::string("diag_axis_") + id, LineParams{
                .from = {sp_origin.position.x, sp_origin.position.y, 0.0f},
                .to   = {sp_tip.position.x, sp_tip.position.y, 0.0f},
                .thickness = 2.0f, .color = col
            });
            l.text(std::string("diag_axis_lbl_") + id, TextSpec{.content = {.value = label}, .font = {.font_size = 11.0f}, .appearance = {.color = col}, .position = {sp_tip.position.x + 4.0f, sp_tip.position.y + 4.0f, 0.0f}});
        };

        l.text("diag_axes_lbl", TextSpec{.content = {.value = "(WORLD)"}, .font = {.font_size = 8.0f}, .appearance = {.color = Color{0.5f, 0.5f, 0.5f, 0.15f}}, .position = {center.x + 20.0f, center.y - 12.0f, 0.0f}});
        draw_axis("X", pose.right(),   Color{1.0f, 0.25f, 0.25f, 0.85f}, "X");
        draw_axis("Y", pose.up(),      Color{0.25f, 1.0f, 0.25f, 0.85f}, "Y");
        draw_axis("Z", pose.forward(), Color{0.25f, 0.40f, 1.0f, 0.85f}, "Z");
    }

    // ── 4. Layer projected bounding boxes ──────────────────────────────
    if (diag.show_projected_bbox) {
        int idx = 0;
        for (const auto& lr : ctx.report.layers) {
            Vec2 b_size = lr.bounds.max - lr.bounds.min;
            if (b_size.x <= 0.0f || b_size.y <= 0.0f) { idx++; continue; }

            Color bc = lr.passed ? Color{0.2f, 0.9f, 0.2f, 0.45f}
                                 : Color{1.0f, 0.2f, 0.2f, 0.55f};

            std::string node_id = "diag_bbox_" + std::to_string(idx);
            l.rect(node_id, RectParams{
                .size = b_size,
                .pos  = {lr.bounds.min.x, lr.bounds.min.y, 0.0f},
                .fill = FillStyle{ .enabled = false },
                .stroke = { .enabled = true, .color = bc, .width = 1.5f }
            });

            std::ostringstream oss;
            oss << lr.name << "  " << std::fixed << std::setprecision(0)
                << (lr.bounds.visible_ratio * 100.0f) << "%";
            if (!lr.passed) oss << " FAIL";
            l.text("diag_bbox_lbl_" + std::to_string(idx), TextSpec{.content = {.value = oss.str()}, .font = {.font_size = 10.0f}, .appearance = {.color = bc}, .position = {lr.bounds.min.x + 4.0f, lr.bounds.min.y + 14.0f, 0.0f}});
            idx++;
        }
    }

    // ── 5. Metrics text panel (top-left corner) ───────────────────────
    if (diag.show_metrics_text) {
        const float panel_x = 10.0f;
        float       panel_y = 10.0f;
        const float line_h  = 18.0f;

        auto draw_line = [&](const std::string& text, const Color& col = Color{1,1,1,1}) {
            l.text("diag_metric_" + std::to_string(static_cast<int>(panel_y)), TextSpec{.content = {.value = text}, .font = {.font_size = 13.0f}, .layout = {.align = TextAlign::Left}, .appearance = {.color = col}, .position = {panel_x, panel_y, 0.0f}});
            panel_y += line_h;
        };

        Color hdr_col = ctx.report.passed ? Color{0.2f, 0.9f, 0.2f, 1.0f}
                                          : Color{1.0f, 0.2f, 0.2f, 1.0f};
        draw_line(ctx.report.composition_name, Color{1.0f, 1.0f, 1.0f, 0.9f});

        {
            std::ostringstream oss;
            oss << "Frame: " << ctx.report.frame;
            draw_line(oss.str(), Color{0.7f, 0.7f, 0.7f, 0.8f});
        }

        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0)
                << "Cam: (" << ctx.camera.position.x << ", " << ctx.camera.position.y << ", " << ctx.camera.position.z << ")";
            draw_line(oss.str(), Color{0.6f, 0.8f, 1.0f, 0.8f});
        }

        {
            Vec3 tw = ctx.camera.point_of_interest_enabled ? ctx.camera.point_of_interest : Vec3{0};
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0)
                << "Tgt: (" << tw.x << ", " << tw.y << ", " << tw.z << ")";
            draw_line(oss.str(), Color{0.8f, 0.7f, 0.4f, 0.8f});
        }

        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
                << "Tgt err: " << ctx.report.target_center_error_px << " px";
            Color ec = ctx.report.target_center_error_px <= 3.0f
                ? Color{0.2f, 0.9f, 0.2f, 0.8f}
                : Color{1.0f, 0.6f, 0.0f, 0.8f};
            draw_line(oss.str(), ec);
        }

        {
            Quat orient = ctx.camera.rotation_quaternion();
            float norm = glm::length(orient);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << "Quat norm: " << norm;
            Color qc = (std::abs(norm - 1.0f) < 1e-4f)
                ? Color{0.2f, 0.9f, 0.2f, 0.8f}
                : Color{1.0f, 0.2f, 0.2f, 0.8f};
            draw_line(oss.str(), qc);
        }

        {
            float total_vis = 0.0f;
            int count = 0;
            for (const auto& lr : ctx.report.layers) {
                if (lr.bounds.visible_ratio > 0.0f) {
                    total_vis += lr.bounds.visible_ratio;
                    count++;
                }
            }
            float avg_vis = count > 0 ? total_vis / static_cast<float>(count) : 0.0f;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "Vis ratio: " << (avg_vis * 100.0f) << "%";
            Color vc = avg_vis > 0.005f ? Color{0.2f, 0.9f, 0.2f, 0.8f}
                                        : Color{1.0f, 0.2f, 0.2f, 0.8f};
            draw_line(oss.str(), vc);
        }

        draw_line(ctx.report.passed ? "PASS" : "FAIL", hdr_col);
    }
}

} // namespace chronon3d
