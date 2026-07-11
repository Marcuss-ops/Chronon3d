// ---------------------------------------------------------------------------
// overlay_kinematic_panels.cpp — Jerk graph + 3D path trace
// Extracted from camera_debug_overlay.cpp
// ---------------------------------------------------------------------------

#include "camera_debug_overlay_panels.hpp"
#include <algorithm>

namespace chronon3d {

void draw_jerk_graph(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    if (!ctx.options.show_camera_path || !ctx.path || ctx.path->samples.empty()) return;

    float graph_w = 400.0f;
    float graph_h = 150.0f;
    float trace_w = 400.0f;
    float trace_h = 200.0f;
    float panel_gap = 10.0f;
    float panel_margin = 20.0f;

    float graph_x, graph_y, trace_x, trace_y;
    switch (ctx.options.anchor) {
        case OverlayAnchor::BottomRight:
            graph_x = ctx.viewport.width - graph_w - panel_margin;
            graph_y = ctx.viewport.height - graph_h - panel_margin;
            trace_x = ctx.viewport.width - trace_w - panel_margin;
            trace_y = graph_y - panel_gap - trace_h;
            break;
        case OverlayAnchor::BottomLeft:
            graph_x = panel_margin;
            graph_y = ctx.viewport.height - graph_h - panel_margin;
            trace_x = panel_margin;
            trace_y = graph_y - panel_gap - trace_h;
            break;
        case OverlayAnchor::TopRight:
            trace_x = ctx.viewport.width - trace_w - panel_margin;
            trace_y = panel_margin;
            graph_x = ctx.viewport.width - graph_w - panel_margin;
            graph_y = trace_y + trace_h + panel_gap;
            break;
        case OverlayAnchor::TopLeft:
        default:
            graph_x = panel_margin;
            graph_y = panel_margin;
            trace_x = panel_margin;
            trace_y = graph_y + graph_h + panel_gap;
            break;
    }
    graph_x += ctx.options.panel_offset_x;
    graph_y += ctx.options.panel_offset_y;
    trace_x += ctx.options.panel_offset_x;
    trace_y += ctx.options.panel_offset_y;

    l.rect("graph_bg", RectParams{
        .size = {graph_w, graph_h}, .pos = {graph_x, graph_y, 0.0f},
        .fill = FillStyle::solid(Color{0.0f, 0.0f, 0.0f, 0.6f}),
        .stroke = { .enabled = true, .color = Color{0.5f, 0.5f, 0.5f, 0.3f}, .width = 1.0f }
    });

    for (int i = 1; i < 4; ++i) {
        float ly = graph_y + (graph_h / 4.0f) * i;
        l.line("graph_grid_" + std::to_string(i), LineParams{
            .from = {graph_x, ly, 0.0f}, .to = {graph_x + graph_w, ly, 0.0f},
            .thickness = 0.5f, .color = Color{0.3f, 0.3f, 0.3f, 0.2f}
        });
    }

    l.text("graph_title", TextSpec{.content = {.value = "CAMERA PATH KINEMATIC JERK (" + std::to_string(ctx.path->current_frame) + "/" + std::to_string(ctx.path->total_frames) + ")"},.position = {graph_x + 10.0f, graph_y + 20.0f, 0.0f},.font = {.font_size = 11.0f},.appearance = {.color = Color{0.9f, 0.9f, 0.9f, 0.8f}}});

    float max_jerk = 0.0f;
    for (const auto& sample : ctx.path->samples) {
        max_jerk = std::max(max_jerk, sample.jerk);
    }
    if (max_jerk < 1e-6f) max_jerk = 0.05f;

    const size_t n = ctx.path->samples.size();
    for (size_t i = 0; i < n; ++i) {
        float t = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
        float px = graph_x + 20.0f + t * (graph_w - 40.0f);

        float step_jerk = ctx.path->samples[i].jerk;
        Color point_color = Color{0.2f, 0.9f, 0.2f, 0.8f};
        if (step_jerk > 0.04f) {
            point_color = Color{1.0f, 0.2f, 0.2f, 0.8f};
        } else if (step_jerk > 0.02f) {
            point_color = Color{1.0f, 0.8f, 0.0f, 0.8f};
        }

        float val_norm = std::min(1.0f, step_jerk / max_jerk);
        float py = graph_y + graph_h - 20.0f - val_norm * (graph_h - 50.0f);

        l.circle("path_pt_" + std::to_string(i), CircleParams{
            .radius = 2.5f, .color = point_color, .pos = {px, py, 0.0f}
        });

        if (i > 0) {
            float prev_t = static_cast<float>(i - 1) / static_cast<float>(n - 1);
            float prev_px = graph_x + 20.0f + prev_t * (graph_w - 40.0f);
            float prev_jerk = ctx.path->samples[i - 1].jerk;
            float prev_val_norm = std::min(1.0f, prev_jerk / max_jerk);
            float prev_py = graph_y + graph_h - 20.0f - prev_val_norm * (graph_h - 50.0f);

            l.line("path_line_" + std::to_string(i), LineParams{
                .from = {prev_px, prev_py, 0.0f}, .to = {px, py, 0.0f},
                .thickness = 1.2f, .color = point_color
            });
        }

        if (i % 15 == 0) {
            l.line("marker_v_" + std::to_string(i), LineParams{
                .from = {px, graph_y + graph_h - 15.0f, 0.0f},
                .to = {px, graph_y + graph_h - 10.0f, 0.0f},
                .thickness = 1.0f, .color = Color{0.7f, 0.7f, 0.7f, 0.5f}
            });
            l.text("marker_txt_" + std::to_string(i), TextSpec{.content = {.value = std::to_string(i)},.position = {px - 5.0f, graph_y + graph_h - 2.0f, 0.0f},.font = {.font_size = 8.0f},.appearance = {.color = Color{0.7f, 0.7f, 0.7f, 0.6f}}});
        }
    }
}

void draw_path_trace(const OverlayContext& ctx) {
    auto& l = ctx.layer;
    if (!ctx.options.show_projected_path || !ctx.path || ctx.path->samples.empty()) return;

    float trace_w = 400.0f;
    float trace_h = 200.0f;
    float panel_margin = 20.0f;
    float panel_gap = 10.0f;
    float graph_h = 150.0f;

    float trace_x, trace_y;
    switch (ctx.options.anchor) {
        case OverlayAnchor::BottomRight:
            trace_x = ctx.viewport.width - trace_w - panel_margin;
            trace_y = ctx.viewport.height - graph_h - panel_margin - panel_gap - trace_h;
            break;
        case OverlayAnchor::BottomLeft:
            trace_x = panel_margin;
            trace_y = ctx.viewport.height - graph_h - panel_margin - panel_gap - trace_h;
            break;
        case OverlayAnchor::TopRight:
            trace_x = ctx.viewport.width - trace_w - panel_margin;
            trace_y = panel_margin;
            break;
        case OverlayAnchor::TopLeft:
        default:
            trace_x = panel_margin;
            trace_y = panel_margin + graph_h + panel_gap;
            break;
    }
    trace_x += ctx.options.panel_offset_x;
    trace_y += ctx.options.panel_offset_y;

    l.rect("trace_bg", RectParams{
        .size = {trace_w, trace_h}, .pos = {trace_x, trace_y, 0.0f},
        .fill = FillStyle::solid(Color{0.0f, 0.0f, 0.05f, 0.55f}),
        .stroke = { .enabled = true, .color = Color{0.4f, 0.4f, 0.6f, 0.3f}, .width = 1.0f }
    });

    l.text("trace_title", TextSpec{.content = {.value = "CAMERA 3D PATH TRACE (" + std::to_string(ctx.path->current_frame) + "/" + std::to_string(ctx.path->total_frames) + ")"},.position = {trace_x + 10.0f, trace_y + 18.0f, 0.0f},.font = {.font_size = 11.0f},.appearance = {.color = Color{0.9f, 0.9f, 0.9f, 0.8f}}});

    const size_t n_proj = ctx.path->samples.size();
    struct ProjectedSample { Vec2 screen_pos; float jerk; bool behind; bool current; };
    std::vector<ProjectedSample> projected;
    projected.reserve(n_proj);

    for (size_t i = 0; i < n_proj; ++i) {
        ScreenPoint psp = project_world_to_screen(ctx.path->samples[i].position, ctx.camera, ctx.viewport);
        projected.push_back({psp.position, ctx.path->samples[i].jerk, psp.behind_camera, static_cast<int>(i) == ctx.path->current_frame});
    }

    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (const auto& ps : projected) {
        if (!ps.behind) {
            min_x = std::min(min_x, ps.screen_pos.x); max_x = std::max(max_x, ps.screen_pos.x);
            min_y = std::min(min_y, ps.screen_pos.y); max_y = std::max(max_y, ps.screen_pos.y);
        }
    }
    if (min_x >= max_x || min_y >= max_y) {
        min_x = ctx.viewport.width * 0.3f; max_x = ctx.viewport.width * 0.7f;
        min_y = ctx.viewport.height * 0.3f; max_y = ctx.viewport.height * 0.7f;
    }

    float margin = 25.0f;
    float draw_w = trace_w - 2.0f * margin;
    float draw_h = trace_h - 35.0f;
    float draw_y0 = trace_y + 30.0f;
    float scale_x = ((max_x - min_x) > 1e-3f) ? draw_w / (max_x - min_x) : 1.0f;
    float scale_y = ((max_y - min_y) > 1e-3f) ? draw_h / (max_y - min_y) : 1.0f;
    float scale = std::min(scale_x, scale_y);

    float center_src_x = (min_x + max_x) * 0.5f;
    float center_src_y = (min_y + max_y) * 0.5f;
    float center_dst_x = trace_x + trace_w * 0.5f;
    float center_dst_y = draw_y0 + draw_h * 0.5f;

    auto to_panel = [&](const Vec2& sp) -> Vec2 {
        return { center_dst_x + (sp.x - center_src_x) * scale, center_dst_y + (sp.y - center_src_y) * scale };
    };

    auto clamp_to_panel = [&](Vec2& p) {
        p.x = std::max(trace_x + margin, std::min(trace_x + trace_w - margin, p.x));
        p.y = std::max(draw_y0, std::min(draw_y0 + draw_h, p.y));
    };

    l.line("trace_axis_h", LineParams{
        .from = {trace_x + margin, center_dst_y, 0.0f}, .to = {trace_x + trace_w - margin, center_dst_y, 0.0f},
        .thickness = 0.5f, .color = Color{0.3f, 0.3f, 0.4f, 0.25f}
    });
    l.line("trace_axis_v", LineParams{
        .from = {center_dst_x, draw_y0, 0.0f}, .to = {center_dst_x, draw_y0 + draw_h, 0.0f},
        .thickness = 0.5f, .color = Color{0.3f, 0.3f, 0.4f, 0.25f}
    });

    for (size_t i = 0; i < n_proj; ++i) {
        if (projected[i].behind) continue;
        Vec2 p = to_panel(projected[i].screen_pos);
        clamp_to_panel(p);

        bool is_past = static_cast<int>(i) <= ctx.path->current_frame;
        float sj = projected[i].jerk;
        Color seg_color;
        if (sj > 0.04f) {
            seg_color = is_past ? Color{1.0f, 0.15f, 0.15f, 0.9f} : Color{1.0f, 0.3f, 0.3f, 0.5f};
        } else if (sj > 0.02f) {
            seg_color = is_past ? Color{1.0f, 0.8f, 0.0f, 0.9f} : Color{1.0f, 0.85f, 0.3f, 0.5f};
        } else {
            seg_color = is_past ? Color{0.15f, 0.85f, 0.2f, 0.85f} : Color{0.25f, 0.7f, 0.3f, 0.45f};
        }

        l.circle("trc_pt_" + std::to_string(i), CircleParams{.radius = 2.0f, .color = seg_color, .pos = {p.x, p.y, 0.0f}});

        if (i > 0 && !projected[i - 1].behind) {
            Vec2 pp = to_panel(projected[i - 1].screen_pos);
            clamp_to_panel(pp);
            l.line("trc_line_" + std::to_string(i), LineParams{
                .from = {pp.x, pp.y, 0.0f}, .to = {p.x, p.y, 0.0f},
                .thickness = 1.5f, .color = seg_color
            });
        }
    }

    // Start marker
    if (!projected.empty() && !projected[0].behind) {
        Vec2 sp_start = to_panel(projected[0].screen_pos);
        clamp_to_panel(sp_start);
        l.circle("trc_start", CircleParams{.radius = 5.0f, .color = Color{0.0f, 0.9f, 1.0f, 0.9f}, .pos = {sp_start.x, sp_start.y, 0.0f}});
        l.text("trc_start_lbl", TextSpec{.content = {.value = "START"},.position = {sp_start.x + 8.0f, sp_start.y - 4.0f, 0.0f},.font = {.font_size = 9.0f},.appearance = {.color = Color{0.0f, 0.9f, 1.0f, 0.8f}}});
    }

    // Current frame marker
    if (ctx.path->current_frame >= 0 && ctx.path->current_frame < static_cast<int>(n_proj)) {
        const auto& cur = projected[ctx.path->current_frame];
        if (!cur.behind) {
            Vec2 sp_cur = to_panel(cur.screen_pos);
            clamp_to_panel(sp_cur);
            l.circle("trc_current", CircleParams{.radius = 6.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.95f}, .pos = {sp_cur.x, sp_cur.y, 0.0f}});
            l.line("trc_cur_h", LineParams{.from = {sp_cur.x - 12.0f, sp_cur.y, 0.0f}, .to = {sp_cur.x + 12.0f, sp_cur.y, 0.0f}, .thickness = 1.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.8f}});
            l.line("trc_cur_v", LineParams{.from = {sp_cur.x, sp_cur.y - 12.0f, 0.0f}, .to = {sp_cur.x, sp_cur.y + 12.0f, 0.0f}, .thickness = 1.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.8f}});
        }
    }

    // Frame markers + legend
    for (size_t i = 0; i < n_proj; i += 15) {
        if (projected[i].behind) continue;
        Vec2 mp = to_panel(projected[i].screen_pos);
        clamp_to_panel(mp);
        l.text("trc_frame_" + std::to_string(i), TextSpec{.content = {.value = std::to_string(i)},.position = {mp.x - 4.0f, mp.y - 14.0f, 0.0f},.font = {.font_size = 8.0f},.appearance = {.color = Color{0.6f, 0.6f, 0.7f, 0.5f}}});
    }

    l.circle("leg_green", CircleParams{.radius = 3.0f, .color = Color{0.15f, 0.85f, 0.2f, 0.85f}, .pos = {trace_x + trace_w - 130.0f, trace_y + 18.0f, 0.0f}});
    l.text("leg_green_t", TextSpec{.content = {.value = "smooth"},.position = {trace_x + trace_w - 122.0f, trace_y + 15.0f, 0.0f},.font = {.font_size = 9.0f},.appearance = {.color = Color{0.7f, 0.7f, 0.7f, 0.6f}}});
    l.circle("leg_yellow", CircleParams{.radius = 3.0f, .color = Color{1.0f, 0.8f, 0.0f, 0.85f}, .pos = {trace_x + trace_w - 80.0f, trace_y + 18.0f, 0.0f}});
    l.text("leg_yellow_t", TextSpec{.content = {.value = "warn"},.position = {trace_x + trace_w - 72.0f, trace_y + 15.0f, 0.0f},.font = {.font_size = 9.0f},.appearance = {.color = Color{0.7f, 0.7f, 0.7f, 0.6f}}});
    l.circle("leg_red", CircleParams{.radius = 3.0f, .color = Color{1.0f, 0.15f, 0.15f, 0.9f}, .pos = {trace_x + trace_w - 38.0f, trace_y + 18.0f, 0.0f}});
    l.text("leg_red_t", TextSpec{.content = {.value = "spike"},.position = {trace_x + trace_w - 30.0f, trace_y + 15.0f, 0.0f},.font = {.font_size = 9.0f},.appearance = {.color = Color{0.7f, 0.7f, 0.7f, 0.6f}}});
}

} // namespace chronon3d
