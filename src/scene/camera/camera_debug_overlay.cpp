#include <chronon3d/scene/camera/camera_debug_overlay.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <algorithm>

namespace chronon3d {

void add_camera_debug_overlay(
    SceneBuilder& s,
    const CameraShotReport& report,
    const Camera2_5D& camera,
    const TransformResolverResult& resolved,
    Viewport viewport,
    CameraDebugOverlayOptions options,
    const CameraPathVisualization* path
) {
    s.layer("camera_debug_hud", [&](LayerBuilder& l) {
        bool safe_area_failed = false;
        for (const auto& f : report.failures) {
            if (f.find("safe area") != std::string::npos || f.find("outside safe") != std::string::npos) {
                safe_area_failed = true;
                break;
            }
        }
        Color safe_area_color = safe_area_failed ? Color{1.0f, 0.2f, 0.2f, 0.7f} : Color{0.2f, 0.9f, 0.2f, 0.5f};

        if (options.show_safe_area) {
            l.rect("safe_area_hud", RectParams{
                .size = {viewport.width * 0.90f, viewport.height * 0.90f},
                .pos = {viewport.width * 0.05f, viewport.height * 0.05f, 0.0f},
                .fill = Fill{ .enabled = false },
                .stroke = { .enabled = true, .color = safe_area_color, .width = 1.5f }
            });
        }

        Vec3 target_world{0.0f, 0.0f, 0.0f};
        bool has_target = false;
        if (!camera.target_name.empty()) {
            auto pos_opt = resolved.world_position(std::string(camera.target_name));
            if (pos_opt) {
                target_world = *pos_opt;
                has_target = true;
            }
        } else if (camera.point_of_interest_enabled) {
            target_world = camera.point_of_interest;
            has_target = true;
        }

        ScreenPoint sp;
        if (has_target) {
            sp = project_world_to_screen(target_world, camera, viewport);
        }

        Color target_color = Color{0.2f, 0.9f, 0.2f, 0.8f}; // green pass
        if (report.target_center_error_px > 3.0f) {
            target_color = Color{1.0f, 0.2f, 0.2f, 0.8f}; // red fail
        } else if (report.target_center_error_px > 1.5f) {
            target_color = Color{1.0f, 0.8f, 0.0f, 0.8f}; // yellow warning
        }

        if (options.show_target) {
            Vec2 center = has_target && !sp.behind_camera ? sp.position : Vec2{viewport.width * 0.5f, viewport.height * 0.5f};
            
            // Horizontal cross line
            l.line("target_cross_h", LineParams{
                .from = {center.x - 25.0f, center.y, 0.0f},
                .to = {center.x + 25.0f, center.y, 0.0f},
                .thickness = 2.0f,
                .color = target_color
            });
            // Vertical cross line
            l.line("target_cross_v", LineParams{
                .from = {center.x, center.y - 25.0f, 0.0f},
                .to = {center.x, center.y + 25.0f, 0.0f},
                .thickness = 2.0f,
                .color = target_color
            });
            // Small circle at center
            l.circle("target_cross_dot", CircleParams{
                .radius = 4.0f,
                .color = target_color,
                .pos = {center.x, center.y, 0.0f}
            });
        }

        // Draw camera path with jerk color coding
        if (options.show_camera_path && path && !path->samples.empty()) {
            float graph_w = 400.0f;
            float graph_h = 150.0f;
            float trace_w = 400.0f;
            float trace_h = 200.0f;
            float panel_gap = 10.0f;
            float panel_margin = 20.0f;
            float total_h = trace_h + panel_gap + graph_h;

            float graph_x, graph_y, trace_x, trace_y;
            switch (options.anchor) {
                case OverlayAnchor::BottomRight:
                    graph_x = viewport.width - graph_w - panel_margin;
                    graph_y = viewport.height - graph_h - panel_margin;
                    trace_x = viewport.width - trace_w - panel_margin;
                    trace_y = graph_y - panel_gap - trace_h;
                    break;
                case OverlayAnchor::BottomLeft:
                    graph_x = panel_margin;
                    graph_y = viewport.height - graph_h - panel_margin;
                    trace_x = panel_margin;
                    trace_y = graph_y - panel_gap - trace_h;
                    break;
                case OverlayAnchor::TopRight:
                    trace_x = viewport.width - trace_w - panel_margin;
                    trace_y = panel_margin;
                    graph_x = viewport.width - graph_w - panel_margin;
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
            graph_x += options.panel_offset_x;
            graph_y += options.panel_offset_y;
            trace_x += options.panel_offset_x;
            trace_y += options.panel_offset_y;

            // Background panel
            l.rect("graph_bg", RectParams{
                .size = {graph_w, graph_h},
                .pos = {graph_x, graph_y, 0.0f},
                .fill = Fill{ .enabled = true, .solid = Color{0.0f, 0.0f, 0.0f, 0.6f} },
                .stroke = { .enabled = true, .color = Color{0.5f, 0.5f, 0.5f, 0.3f}, .width = 1.0f }
            });

            // Grid lines
            for (int i = 1; i < 4; ++i) {
                float ly = graph_y + (graph_h / 4.0f) * i;
                l.line("graph_grid_" + std::to_string(i), LineParams{
                    .from = {graph_x, ly, 0.0f},
                    .to = {graph_x + graph_w, ly, 0.0f},
                    .thickness = 0.5f,
                    .color = Color{0.3f, 0.3f, 0.3f, 0.2f}
                });
            }

            l.text("graph_title", TextParams{
                .text = "CAMERA PATH KINEMATIC JERK (" + std::to_string(path->current_frame) + "/" + std::to_string(path->total_frames) + ")",
                .pos = {graph_x + 10.0f, graph_y + 20.0f, 0.0f},
                .font_size = 11.0f,
                .color = Color{0.9f, 0.9f, 0.9f, 0.8f}
            });

            // Find max jerk for normalization
            float max_jerk = 0.0f;
            for (const auto& sample : path->samples) {
                max_jerk = std::max(max_jerk, sample.jerk);
            }
            if (max_jerk < 1e-6f) max_jerk = 0.05f;

            const size_t n = path->samples.size();
            for (size_t i = 0; i < n; ++i) {
                float t = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
                float px = graph_x + 20.0f + t * (graph_w - 40.0f);

                float step_jerk = path->samples[i].jerk;
                Color point_color = Color{0.2f, 0.9f, 0.2f, 0.8f}; // green smooth
                if (step_jerk > 0.04f) {
                    point_color = Color{1.0f, 0.2f, 0.2f, 0.8f}; // red spike
                } else if (step_jerk > 0.02f) {
                    point_color = Color{1.0f, 0.8f, 0.0f, 0.8f}; // yellow warning
                }

                float val_norm = std::min(1.0f, step_jerk / max_jerk);
                float py = graph_y + graph_h - 20.0f - val_norm * (graph_h - 50.0f);

                l.circle("path_pt_" + std::to_string(i), CircleParams{
                    .radius = 2.5f,
                    .color = point_color,
                    .pos = {px, py, 0.0f}
                });

                if (i > 0) {
                    float prev_t = static_cast<float>(i - 1) / static_cast<float>(n - 1);
                    float prev_px = graph_x + 20.0f + prev_t * (graph_w - 40.0f);
                    float prev_jerk = path->samples[i - 1].jerk;
                    float prev_val_norm = std::min(1.0f, prev_jerk / max_jerk);
                    float prev_py = graph_y + graph_h - 20.0f - prev_val_norm * (graph_h - 50.0f);

                    l.line("path_line_" + std::to_string(i), LineParams{
                        .from = {prev_px, prev_py, 0.0f},
                        .to = {px, py, 0.0f},
                        .thickness = 1.2f,
                        .color = point_color
                    });
                }

                // Frame markers every 15 samples
                if (i % 15 == 0) {
                    l.line("marker_v_" + std::to_string(i), LineParams{
                        .from = {px, graph_y + graph_h - 15.0f, 0.0f},
                        .to = {px, graph_y + graph_h - 10.0f, 0.0f},
                        .thickness = 1.0f,
                        .color = Color{0.7f, 0.7f, 0.7f, 0.5f}
                    });
                    l.text("marker_txt_" + std::to_string(i), TextParams{
                        .text = std::to_string(i),
                        .pos = {px - 5.0f, graph_y + graph_h - 2.0f, 0.0f},
                        .font_size = 8.0f,
                        .color = Color{0.7f, 0.7f, 0.7f, 0.6f}
                    });
                }
            }
        }

        // Draw 3D projected camera path trace on screen
        if (options.show_projected_path && path && !path->samples.empty()) {
            float trace_w = 400.0f;
            float trace_h = 200.0f;
            float panel_margin = 20.0f;
            float panel_gap = 10.0f;
            float graph_h = 150.0f;

            float trace_x, trace_y;
            switch (options.anchor) {
                case OverlayAnchor::BottomRight:
                    trace_x = viewport.width - trace_w - panel_margin;
                    trace_y = viewport.height - graph_h - panel_margin - panel_gap - trace_h;
                    break;
                case OverlayAnchor::BottomLeft:
                    trace_x = panel_margin;
                    trace_y = viewport.height - graph_h - panel_margin - panel_gap - trace_h;
                    break;
                case OverlayAnchor::TopRight:
                    trace_x = viewport.width - trace_w - panel_margin;
                    trace_y = panel_margin;
                    break;
                case OverlayAnchor::TopLeft:
                default:
                    trace_x = panel_margin;
                    trace_y = panel_margin + graph_h + panel_gap;
                    break;
            }
            trace_x += options.panel_offset_x;
            trace_y += options.panel_offset_y;

            // Background panel
            l.rect("trace_bg", RectParams{
                .size = {trace_w, trace_h},
                .pos = {trace_x, trace_y, 0.0f},
                .fill = Fill{ .enabled = true, .solid = Color{0.0f, 0.0f, 0.05f, 0.55f} },
                .stroke = { .enabled = true, .color = Color{0.4f, 0.4f, 0.6f, 0.3f}, .width = 1.0f }
            });

            l.text("trace_title", TextParams{
                .text = "CAMERA 3D PATH TRACE (" + std::to_string(path->current_frame) + "/" + std::to_string(path->total_frames) + ")",
                .pos = {trace_x + 10.0f, trace_y + 18.0f, 0.0f},
                .font_size = 11.0f,
                .color = Color{0.9f, 0.9f, 0.9f, 0.8f}
            });

            // Project all camera positions through current frame's camera
            const size_t n_proj = path->samples.size();
            struct ProjectedSample {
                Vec2 screen_pos;
                float jerk;
                bool behind;
                bool current;
            };
            std::vector<ProjectedSample> projected;
            projected.reserve(n_proj);

            for (size_t i = 0; i < n_proj; ++i) {
                ScreenPoint psp = project_world_to_screen(path->samples[i].position, camera, viewport);
                projected.push_back({
                    psp.position,
                    path->samples[i].jerk,
                    psp.behind_camera,
                    static_cast<int>(i) == path->current_frame
                });
            }

            // Compute bounds of visible points to auto-fit inside the panel
            float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
            for (const auto& ps : projected) {
                if (!ps.behind) {
                    min_x = std::min(min_x, ps.screen_pos.x);
                    max_x = std::max(max_x, ps.screen_pos.x);
                    min_y = std::min(min_y, ps.screen_pos.y);
                    max_y = std::max(max_y, ps.screen_pos.y);
                }
            }

            // Fallback if all behind or degenerate
            if (min_x >= max_x || min_y >= max_y) {
                min_x = viewport.width * 0.3f; max_x = viewport.width * 0.7f;
                min_y = viewport.height * 0.3f; max_y = viewport.height * 0.7f;
            }

            float range_x = max_x - min_x;
            float range_y = max_y - min_y;
            float margin = 25.0f;
            float draw_w = trace_w - 2.0f * margin;
            float draw_h = trace_h - 35.0f; // below title
            float draw_y0 = trace_y + 30.0f;
            float scale_x = (range_x > 1e-3f) ? draw_w / range_x : 1.0f;
            float scale_y = (range_y > 1e-3f) ? draw_h / range_y : 1.0f;
            float scale = std::min(scale_x, scale_y);

            // Center offset
            float center_src_x = (min_x + max_x) * 0.5f;
            float center_src_y = (min_y + max_y) * 0.5f;
            float center_dst_x = trace_x + trace_w * 0.5f;
            float center_dst_y = draw_y0 + draw_h * 0.5f;

            auto to_panel = [&](const Vec2& sp) -> Vec2 {
                return {
                    center_dst_x + (sp.x - center_src_x) * scale,
                    center_dst_y + (sp.y - center_src_y) * scale
                };
            };

            // Draw axis crosshair (subtle)
            l.line("trace_axis_h", LineParams{
                .from = {trace_x + margin, center_dst_y, 0.0f},
                .to = {trace_x + trace_w - margin, center_dst_y, 0.0f},
                .thickness = 0.5f,
                .color = Color{0.3f, 0.3f, 0.4f, 0.25f}
            });
            l.line("trace_axis_v", LineParams{
                .from = {center_dst_x, draw_y0, 0.0f},
                .to = {center_dst_x, draw_y0 + draw_h, 0.0f},
                .thickness = 0.5f,
                .color = Color{0.3f, 0.3f, 0.4f, 0.25f}
            });

            // Draw path segments (past = dimmer, future = brighter)
            for (size_t i = 0; i < n_proj; ++i) {
                if (projected[i].behind) continue;
                Vec2 p = to_panel(projected[i].screen_pos);

                // Clamp to panel bounds
                p.x = std::max(trace_x + margin, std::min(trace_x + trace_w - margin, p.x));
                p.y = std::max(draw_y0, std::min(draw_y0 + draw_h, p.y));

                bool is_past = static_cast<int>(i) <= path->current_frame;

                // Color by jerk
                float sj = projected[i].jerk;
                Color seg_color;
                if (sj > 0.04f) {
                    seg_color = is_past ? Color{1.0f, 0.15f, 0.15f, 0.9f} : Color{1.0f, 0.3f, 0.3f, 0.5f};
                } else if (sj > 0.02f) {
                    seg_color = is_past ? Color{1.0f, 0.8f, 0.0f, 0.9f} : Color{1.0f, 0.85f, 0.3f, 0.5f};
                } else {
                    seg_color = is_past ? Color{0.15f, 0.85f, 0.2f, 0.85f} : Color{0.25f, 0.7f, 0.3f, 0.45f};
                }

                // Draw point
                l.circle("trc_pt_" + std::to_string(i), CircleParams{
                    .radius = 2.0f,
                    .color = seg_color,
                    .pos = {p.x, p.y, 0.0f}
                });

                // Draw segment to previous
                if (i > 0 && !projected[i - 1].behind) {
                    Vec2 pp = to_panel(projected[i - 1].screen_pos);
                    pp.x = std::max(trace_x + margin, std::min(trace_x + trace_w - margin, pp.x));
                    pp.y = std::max(draw_y0, std::min(draw_y0 + draw_h, pp.y));

                    l.line("trc_line_" + std::to_string(i), LineParams{
                        .from = {pp.x, pp.y, 0.0f},
                        .to = {p.x, p.y, 0.0f},
                        .thickness = 1.5f,
                        .color = seg_color
                    });
                }
            }

            // Draw start marker (cyan diamond)
            if (!projected.empty() && !projected[0].behind) {
                Vec2 sp_start = to_panel(projected[0].screen_pos);
                sp_start.x = std::max(trace_x + margin, std::min(trace_x + trace_w - margin, sp_start.x));
                sp_start.y = std::max(draw_y0, std::min(draw_y0 + draw_h, sp_start.y));
                l.circle("trc_start", CircleParams{
                    .radius = 5.0f,
                    .color = Color{0.0f, 0.9f, 1.0f, 0.9f},
                    .pos = {sp_start.x, sp_start.y, 0.0f}
                });
                l.text("trc_start_lbl", TextParams{
                    .text = "START",
                    .pos = {sp_start.x + 8.0f, sp_start.y - 4.0f, 0.0f},
                    .font_size = 9.0f,
                    .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                });
            }

            // Draw current frame marker (bright white crosshair)
            if (path->current_frame >= 0 && path->current_frame < static_cast<int>(n_proj)) {
                const auto& cur = projected[path->current_frame];
                if (!cur.behind) {
                    Vec2 sp_cur = to_panel(cur.screen_pos);
                    sp_cur.x = std::max(trace_x + margin, std::min(trace_x + trace_w - margin, sp_cur.x));
                    sp_cur.y = std::max(draw_y0, std::min(draw_y0 + draw_h, sp_cur.y));
                    l.circle("trc_current", CircleParams{
                        .radius = 6.0f,
                        .color = Color{1.0f, 1.0f, 1.0f, 0.95f},
                        .pos = {sp_cur.x, sp_cur.y, 0.0f}
                    });
                    // Crosshair lines
                    l.line("trc_cur_h", LineParams{
                        .from = {sp_cur.x - 12.0f, sp_cur.y, 0.0f},
                        .to = {sp_cur.x + 12.0f, sp_cur.y, 0.0f},
                        .thickness = 1.0f,
                        .color = Color{1.0f, 1.0f, 1.0f, 0.8f}
                    });
                    l.line("trc_cur_v", LineParams{
                        .from = {sp_cur.x, sp_cur.y - 12.0f, 0.0f},
                        .to = {sp_cur.x, sp_cur.y + 12.0f, 0.0f},
                        .thickness = 1.0f,
                        .color = Color{1.0f, 1.0f, 1.0f, 0.8f}
                    });
                }
            }

            // Frame markers every 15 samples
            for (size_t i = 0; i < n_proj; i += 15) {
                if (projected[i].behind) continue;
                Vec2 mp = to_panel(projected[i].screen_pos);
                mp.x = std::max(trace_x + margin, std::min(trace_x + trace_w - margin, mp.x));
                mp.y = std::max(draw_y0, std::min(draw_y0 + draw_h, mp.y));
                l.text("trc_frame_" + std::to_string(i), TextParams{
                    .text = std::to_string(i),
                    .pos = {mp.x - 4.0f, mp.y - 14.0f, 0.0f},
                    .font_size = 8.0f,
                    .color = Color{0.6f, 0.6f, 0.7f, 0.5f}
                });
            }

            // Legend
            l.circle("leg_green", CircleParams{ .radius = 3.0f, .color = Color{0.15f, 0.85f, 0.2f, 0.85f}, .pos = {trace_x + trace_w - 130.0f, trace_y + 18.0f, 0.0f} });
            l.text("leg_green_t", TextParams{ .text = "smooth", .pos = {trace_x + trace_w - 122.0f, trace_y + 15.0f, 0.0f}, .font_size = 9.0f, .color = Color{0.7f, 0.7f, 0.7f, 0.6f} });
            l.circle("leg_yellow", CircleParams{ .radius = 3.0f, .color = Color{1.0f, 0.8f, 0.0f, 0.85f}, .pos = {trace_x + trace_w - 80.0f, trace_y + 18.0f, 0.0f} });
            l.text("leg_yellow_t", TextParams{ .text = "warn", .pos = {trace_x + trace_w - 72.0f, trace_y + 15.0f, 0.0f}, .font_size = 9.0f, .color = Color{0.7f, 0.7f, 0.7f, 0.6f} });
            l.circle("leg_red", CircleParams{ .radius = 3.0f, .color = Color{1.0f, 0.15f, 0.15f, 0.9f}, .pos = {trace_x + trace_w - 38.0f, trace_y + 18.0f, 0.0f} });
            l.text("leg_red_t", TextParams{ .text = "spike", .pos = {trace_x + trace_w - 30.0f, trace_y + 15.0f, 0.0f}, .font_size = 9.0f, .color = Color{0.7f, 0.7f, 0.7f, 0.6f} });
        }

        if (options.show_camera_to_target_line && has_target && !sp.behind_camera) {
            l.line("camera_to_target_line", LineParams{
                .from = {viewport.width * 0.5f, viewport.height * 0.5f, 0.0f},
                .to = {sp.position.x, sp.position.y, 0.0f},
                .thickness = 1.5f,
                .color = target_color
            });
            
            float dist = glm::distance(sp.position, Vec2{viewport.width * 0.5f, viewport.height * 0.5f});
            if (dist > 1.0f) {
                l.text("target_deviation_text", TextParams{
                    .text = std::to_string(static_cast<int>(dist)) + "px error",
                    .pos = {(viewport.width * 0.5f + sp.position.x) * 0.5f + 10.0f,
                            (viewport.height * 0.5f + sp.position.y) * 0.5f + 10.0f,
                            0.0f},
                    .font_size = 12.0f,
                    .color = target_color
                });
            }
        }

        // Draw debug crosses for null/parent layers (match _null or _parent suffix/substring)
        for (const auto& pair : resolved.resolved) {
            const std::string& name = pair.first;
            if (name.find("_null") != std::string::npos || name.find("_parent") != std::string::npos) {
                Vec3 null_pos = Vec3(pair.second.world_matrix[3]);
                ScreenPoint n_sp = project_world_to_screen(null_pos, camera, viewport);
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
                        .thickness = 1.0f,
                        .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                    });
                    l.line("null_parent_v_" + name, LineParams{
                        .from = {n_sp.position.x, n_sp.position.y - 12.0f, 0.0f},
                        .to = {n_sp.position.x, n_sp.position.y + 12.0f, 0.0f},
                        .thickness = 1.0f,
                        .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                    });
                    l.text("null_parent_lbl_" + name, TextParams{
                        .text = name,
                        .pos = {n_sp.position.x + 15.0f, n_sp.position.y + 5.0f, 0.0f},
                        .font_size = 10.0f,
                        .color = Color{0.0f, 0.9f, 1.0f, 0.8f}
                    });
                }
            }
        }

        // Draw parent-child links/lines in the HUD
        for (const auto& pair : resolved.resolved) {
            const std::string& child_name = pair.first;
            const ResolvedTransform3D& r3d = pair.second;
            if (!r3d.local.parent_name.empty()) {
                auto parent_pos_opt = resolved.world_position(r3d.local.parent_name);
                auto child_pos_opt = resolved.world_position(child_name);
                if (parent_pos_opt && child_pos_opt) {
                    ScreenPoint parent_sp = project_world_to_screen(*parent_pos_opt, camera, viewport);
                    ScreenPoint child_sp = project_world_to_screen(*child_pos_opt, camera, viewport);
                    if (!parent_sp.behind_camera && !child_sp.behind_camera) {
                        l.line("link_" + r3d.local.parent_name + "_" + child_name, LineParams{
                            .from = {parent_sp.position.x, parent_sp.position.y, 0.0f},
                            .to = {child_sp.position.x, child_sp.position.y, 0.0f},
                            .thickness = 1.0f,
                            .color = Color{0.0f, 0.7f, 1.0f, 0.4f}
                        });
                    }
                }
            }
        }

        // Top-down orthographic preview (bird's-eye view from Y=50)
        if (options.show_topdown_preview) {
            float td_w = 280.0f;
            float td_h = 220.0f;
            float td_margin = 20.0f;
            float panel_gap = 10.0f;
            // Position diagonally opposite to the path panels anchor to avoid overlap
            float td_x, td_y;
            switch (options.anchor) {
                case OverlayAnchor::BottomRight:
                    td_x = td_margin;
                    td_y = td_margin;
                    break;
                case OverlayAnchor::BottomLeft:
                    td_x = viewport.width - td_w - td_margin;
                    td_y = td_margin;
                    break;
                case OverlayAnchor::TopRight:
                    td_x = td_margin;
                    td_y = viewport.height - td_h - td_margin;
                    break;
                case OverlayAnchor::TopLeft:
                default:
                    td_x = viewport.width - td_w - td_margin;
                    td_y = viewport.height - td_h - td_margin;
                    break;
            }
            td_x += options.panel_offset_x;
            td_y += options.panel_offset_y;

            // Background
            l.rect("topdown_bg", RectParams{
                .size = {td_w, td_h},
                .pos = {td_x, td_y, 0.0f},
                .fill = Fill{ .enabled = true, .solid = Color{0.0f, 0.02f, 0.05f, 0.7f} },
                .stroke = { .enabled = true, .color = Color{0.3f, 0.5f, 0.8f, 0.35f}, .width = 1.0f }
            });

            l.text("topdown_title", TextParams{
                .text = "TOP-DOWN VIEW (XZ)",
                .pos = {td_x + 10.0f, td_y + 16.0f, 0.0f},
                .font_size = 10.0f,
                .color = Color{0.8f, 0.85f, 1.0f, 0.8f}
            });

            // Compute bounds of all resolved positions to auto-fit
            float world_min_x = 1e9f, world_max_x = -1e9f;
            float world_min_z = 1e9f, world_max_z = -1e9f;
            for (const auto& pair : resolved.resolved) {
                Vec3 pos(pair.second.world_matrix[3]);
                world_min_x = std::min(world_min_x, pos.x);
                world_max_x = std::max(world_max_x, pos.x);
                world_min_z = std::min(world_min_z, pos.z);
                world_max_z = std::max(world_max_z, pos.z);
            }
            // Include camera position
            world_min_x = std::min(world_min_x, camera.position.x);
            world_max_x = std::max(world_max_x, camera.position.x);
            world_min_z = std::min(world_min_z, camera.position.z);
            world_max_z = std::max(world_max_z, camera.position.z);

            // Fallback if degenerate
            if (world_min_x >= world_max_x) { world_min_x -= 200.0f; world_max_x += 200.0f; }
            if (world_min_z >= world_max_z) { world_min_z -= 200.0f; world_max_z += 200.0f; }

            // Add padding
            float range_x = world_max_x - world_min_x;
            float range_z = world_max_z - world_min_z;
            float max_range = std::max(range_x, range_z);
            float pad = max_range * 0.15f;
            world_min_x -= pad; world_max_x += pad;
            world_min_z -= pad; world_max_z += pad;
            range_x = world_max_x - world_min_x;
            range_z = world_max_z - world_min_z;

            float draw_margin = 22.0f;
            float draw_w = td_w - 2.0f * draw_margin;
            float draw_h = td_h - 36.0f;
            float draw_y0 = td_y + 28.0f;
            float sx = (range_x > 1e-3f) ? draw_w / range_x : 1.0f;
            float sz = (range_z > 1e-3f) ? draw_h / range_z : 1.0f;
            float s = std::min(sx, sz);

            // Mapping: world X → screen X, world Z → screen Y (inverted: positive Z = top)
            auto to_td = [&](float wx, float wz) -> Vec2 {
                return {
                    td_x + draw_margin + (wx - world_min_x) * s,
                    draw_y0 + draw_h - (wz - world_min_z) * s  // Z inverted: positive Z = up
                };
            };

            // Grid lines (subtle)
            l.line("td_grid_h", LineParams{
                .from = {td_x + draw_margin, draw_y0 + draw_h * 0.5f, 0.0f},
                .to = {td_x + td_w - draw_margin, draw_y0 + draw_h * 0.5f, 0.0f},
                .thickness = 0.5f, .color = Color{0.25f, 0.35f, 0.55f, 0.2f}
            });
            l.line("td_grid_v", LineParams{
                .from = {td_x + draw_margin + draw_w * 0.5f, draw_y0, 0.0f},
                .to = {td_x + draw_margin + draw_w * 0.5f, draw_y0 + draw_h, 0.0f},
                .thickness = 0.5f, .color = Color{0.25f, 0.35f, 0.55f, 0.2f}
            });

            // Axis labels
            l.text("td_axis_x", TextParams{ .text = "+X", .pos = {td_x + td_w - draw_margin - 14.0f, draw_y0 + draw_h + 2.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.4f, 0.4f, 0.5f} });
            l.text("td_axis_z", TextParams{ .text = "+Z", .pos = {td_x + draw_margin - 2.0f, draw_y0 - 6.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.4f, 0.4f, 0.6f, 0.5f} });

            // Draw resolved layers as colored rectangles at their XZ position
            int td_idx = 0;
            for (const auto& pair : resolved.resolved) {
                Vec3 pos(pair.second.world_matrix[3]);
                Vec2 screen = to_td(pos.x, pos.z);

                // Skip if outside panel
                if (screen.x < td_x + draw_margin || screen.x > td_x + td_w - draw_margin ||
                    screen.y < draw_y0 || screen.y > draw_y0 + draw_h) {
                    continue;
                }

                // Color based on Z depth (blue=far, green=center, red=near)
                float z_norm = (range_z > 1e-3f) ? (pos.z - world_min_z) / range_z : 0.5f;
                Color layer_color;
                if (pos.z < 0.0f) {
                    // Near (front of camera)
                    layer_color = Color{0.2f + z_norm * 0.6f, 0.8f, 1.0f, 0.85f};
                } else if (pos.z > 0.0f) {
                    // Far (behind camera target)
                    layer_color = Color{0.3f, 0.4f + z_norm * 0.3f, 0.9f - z_norm * 0.3f, 0.75f};
                } else {
                    layer_color = Color{0.2f, 0.9f, 0.2f, 0.85f}; // Center = green
                }

                // Check if this layer is in the report for pass/fail coloring
                for (const auto& lr : report.layers) {
                    if (lr.name == pair.first) {
                        layer_color = lr.passed ? Color{0.2f, 0.9f, 0.2f, 0.85f} : Color{1.0f, 0.3f, 0.15f, 0.85f};
                        break;
                    }
                }

                // Check if it's a null/parent layer
                bool is_null = pair.first.find("_null") != std::string::npos || pair.first.find("_parent") != std::string::npos;
                if (is_null) {
                    layer_color = Color{0.0f, 0.9f, 1.0f, 0.8f};
                }

                float dot_r = is_null ? 4.0f : 3.5f;
                l.circle("td_layer_" + std::to_string(td_idx), CircleParams{
                    .radius = dot_r,
                    .color = layer_color,
                    .pos = {screen.x, screen.y, 0.0f}
                });

                // Label (only for non-null, first 8 layers to avoid clutter)
                if (!is_null && td_idx < 8) {
                    l.text("td_lbl_" + std::to_string(td_idx), TextParams{
                        .text = pair.first,
                        .pos = {screen.x + 5.0f, screen.y - 4.0f, 0.0f},
                        .font_size = 7.0f,
                        .color = Color{0.7f, 0.75f, 0.9f, 0.55f}
                    });
                }
                td_idx++;
            }

            // Draw camera position and direction
            Vec2 cam_screen = to_td(camera.position.x, camera.position.z);
            if (cam_screen.x >= td_x + draw_margin && cam_screen.x <= td_x + td_w - draw_margin &&
                cam_screen.y >= draw_y0 && cam_screen.y <= draw_y0 + draw_h) {
                // Camera position = white triangle
                l.circle("td_cam_pos", CircleParams{
                    .radius = 5.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.95f},
                    .pos = {cam_screen.x, cam_screen.y, 0.0f}
                });

                // Camera direction (project yaw to XZ)
                float yaw_rad = glm::radians(camera.rotation.y);
                float dir_x = std::sin(yaw_rad);
                float dir_z = -std::cos(yaw_rad);
                float dir_len = 40.0f;

                l.line("td_cam_dir", LineParams{
                    .from = {cam_screen.x, cam_screen.y, 0.0f},
                    .to = {cam_screen.x + dir_x * dir_len, cam_screen.y - dir_z * dir_len, 0.0f},
                    .thickness = 2.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.8f}
                });

                // FOV cone (two side lines)
                float fov_half = glm::radians(camera.fov_deg * 0.5f);
                float cone_len = 30.0f;
                float left_x = std::sin(yaw_rad - fov_half);
                float left_z = -std::cos(yaw_rad - fov_half);
                float right_x = std::sin(yaw_rad + fov_half);
                float right_z = -std::cos(yaw_rad + fov_half);

                l.line("td_fov_l", LineParams{
                    .from = {cam_screen.x, cam_screen.y, 0.0f},
                    .to = {cam_screen.x + left_x * cone_len, cam_screen.y - left_z * cone_len, 0.0f},
                    .thickness = 1.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.35f}
                });
                l.line("td_fov_r", LineParams{
                    .from = {cam_screen.x, cam_screen.y, 0.0f},
                    .to = {cam_screen.x + right_x * cone_len, cam_screen.y - right_z * cone_len, 0.0f},
                    .thickness = 1.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.35f}
                });

                l.text("td_cam_lbl", TextParams{
                    .text = "CAM",
                    .pos = {cam_screen.x + 7.0f, cam_screen.y - 8.0f, 0.0f},
                    .font_size = 8.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.8f}
                });
            }

            // Legend at bottom of panel
            l.circle("td_leg_near", CircleParams{ .radius = 3.0f, .color = Color{0.4f, 0.9f, 1.0f, 0.85f}, .pos = {td_x + 10.0f, td_y + td_h - 8.0f, 0.0f} });
            l.text("td_leg_near_t", TextParams{ .text = "near", .pos = {td_x + 16.0f, td_y + td_h - 12.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.6f, 0.7f, 0.5f} });
            l.circle("td_leg_far", CircleParams{ .radius = 3.0f, .color = Color{0.3f, 0.5f, 0.9f, 0.75f}, .pos = {td_x + 52.0f, td_y + td_h - 8.0f, 0.0f} });
            l.text("td_leg_far_t", TextParams{ .text = "far", .pos = {td_x + 58.0f, td_y + td_h - 12.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.6f, 0.7f, 0.5f} });
            l.circle("td_leg_cam", CircleParams{ .radius = 3.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.9f}, .pos = {td_x + 88.0f, td_y + td_h - 8.0f, 0.0f} });
            l.text("td_leg_cam_t", TextParams{ .text = "cam", .pos = {td_x + 94.0f, td_y + td_h - 12.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.6f, 0.7f, 0.5f} });
        }

        // Side-view depth map (XZ lateral view: X=horizontal, Z=height)
        if (options.show_depth_side_view) {
            float sv_w = 280.0f;
            float sv_h = 220.0f;
            float sv_margin = 20.0f;

            // Position next to the top-down panel (right of it for bottom-right anchor)
            float sv_x, sv_y;
            switch (options.anchor) {
                case OverlayAnchor::BottomRight:
                    sv_x = sv_margin + 280.0f + 10.0f;
                    sv_y = sv_margin;
                    break;
                case OverlayAnchor::BottomLeft:
                    sv_x = viewport.width - 2.0f * sv_w - 2.0f * sv_margin - 10.0f;
                    sv_y = sv_margin;
                    break;
                case OverlayAnchor::TopRight:
                    sv_x = sv_margin + 280.0f + 10.0f;
                    sv_y = viewport.height - sv_h - sv_margin;
                    break;
                case OverlayAnchor::TopLeft:
                default:
                    sv_x = viewport.width - 2.0f * sv_w - 2.0f * sv_margin - 10.0f;
                    sv_y = viewport.height - sv_h - sv_margin;
                    break;
            }
            sv_x += options.panel_offset_x;
            sv_y += options.panel_offset_y;

            // Background
            l.rect("sideview_bg", RectParams{
                .size = {sv_w, sv_h},
                .pos = {sv_x, sv_y, 0.0f},
                .fill = Fill{ .enabled = true, .solid = Color{0.0f, 0.03f, 0.02f, 0.7f} },
                .stroke = { .enabled = true, .color = Color{0.3f, 0.6f, 0.5f, 0.35f}, .width = 1.0f }
            });

            l.text("sv_title", TextParams{
                .text = "DEPTH SIDE VIEW (X vs Z)",
                .pos = {sv_x + 10.0f, sv_y + 16.0f, 0.0f},
                .font_size = 10.0f,
                .color = Color{0.8f, 1.0f, 0.85f, 0.8f}
            });

            // Compute bounds of all resolved positions to auto-fit
            float w_min_x = 1e9f, w_max_x = -1e9f;
            float w_min_z = 1e9f, w_max_z = -1e9f;
            for (const auto& pair : resolved.resolved) {
                Vec3 pos(pair.second.world_matrix[3]);
                w_min_x = std::min(w_min_x, pos.x);
                w_max_x = std::max(w_max_x, pos.x);
                w_min_z = std::min(w_min_z, pos.z);
                w_max_z = std::max(w_max_z, pos.z);
            }
            w_min_x = std::min(w_min_x, camera.position.x);
            w_max_x = std::max(w_max_x, camera.position.x);
            w_min_z = std::min(w_min_z, camera.position.z);
            w_max_z = std::max(w_max_z, camera.position.z);

            if (w_min_x >= w_max_x) { w_min_x -= 200.0f; w_max_x += 200.0f; }
            if (w_min_z >= w_max_z) { w_min_z -= 200.0f; w_max_z += 200.0f; }

            float range_x = w_max_x - w_min_x;
            float range_z = w_max_z - w_min_z;
            float pad_x = range_x * 0.15f;
            float pad_z = range_z * 0.15f;
            w_min_x -= pad_x; w_max_x += pad_x;
            w_min_z -= pad_z; w_max_z += pad_z;
            range_x = w_max_x - w_min_x;
            range_z = w_max_z - w_min_z;

            float d_margin = 22.0f;
            float d_w = sv_w - 2.0f * d_margin;
            float d_h = sv_h - 36.0f;
            float d_y0 = sv_y + 28.0f;
            float sx = (range_x > 1e-3f) ? d_w / range_x : 1.0f;
            float sz = (range_z > 1e-3f) ? d_h / range_z : 1.0f;
            float s = std::min(sx, sz);

            // Map: world X → horizontal, world Z → vertical (positive Z = UP = further from camera)
            auto to_sv = [&](float wx, float wz) -> Vec2 {
                return {
                    sv_x + d_margin + (wx - w_min_x) * s,
                    d_y0 + d_h - (wz - w_min_z) * s  // Z inverted: positive Z = top
                };
            };

            // Grid lines
            l.line("sv_grid_h", LineParams{
                .from = {sv_x + d_margin, d_y0 + d_h * 0.5f, 0.0f},
                .to = {sv_x + sv_w - d_margin, d_y0 + d_h * 0.5f, 0.0f},
                .thickness = 0.5f, .color = Color{0.25f, 0.45f, 0.35f, 0.2f}
            });
            l.line("sv_grid_v", LineParams{
                .from = {sv_x + d_margin + d_w * 0.5f, d_y0, 0.0f},
                .to = {sv_x + d_margin + d_w * 0.5f, d_y0 + d_h, 0.0f},
                .thickness = 0.5f, .color = Color{0.25f, 0.45f, 0.35f, 0.2f}
            });

            // Z=0 horizontal line (reference plane)
            if (w_min_z < 0.0f && w_max_z > 0.0f) {
                float z0_y = d_y0 + d_h - (0.0f - w_min_z) * s;
                l.line("sv_z0", LineParams{
                    .from = {sv_x + d_margin, z0_y, 0.0f},
                    .to = {sv_x + sv_w - d_margin, z0_y, 0.0f},
                    .thickness = 1.0f, .color = Color{0.5f, 0.8f, 0.5f, 0.3f}
                });
                l.text("sv_z0_lbl", TextParams{ .text = "Z=0", .pos = {sv_x + sv_w - d_margin + 2.0f, z0_y - 4.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.5f, 0.8f, 0.5f, 0.5f} });
            }

            // Axis labels
            l.text("sv_axis_x", TextParams{ .text = "+X", .pos = {sv_x + sv_w - d_margin - 14.0f, d_y0 + d_h + 2.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.5f, 0.4f, 0.5f} });
            l.text("sv_axis_z", TextParams{ .text = "+Z", .pos = {sv_x + d_margin - 2.0f, d_y0 - 6.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.4f, 0.6f, 0.4f, 0.5f} });

            // Draw each resolved layer as a horizontal bar (width = layer extent, height = Z position)
            int sv_idx = 0;
            for (const auto& pair : resolved.resolved) {
                Vec3 pos(pair.second.world_matrix[3]);
                Vec2 screen = to_sv(pos.x, pos.z);

                if (screen.x < sv_x + d_margin || screen.x > sv_x + sv_w - d_margin ||
                    screen.y < d_y0 || screen.y > d_y0 + d_h) {
                    continue;
                }

                // Check if in report
                bool in_report = false;
                bool passed = true;
                for (const auto& lr : report.layers) {
                    if (lr.name == pair.first) {
                        in_report = true;
                        passed = lr.passed;
                        break;
                    }
                }

                bool is_null = pair.first.find("_null") != std::string::npos || pair.first.find("_parent") != std::string::npos;

                Color bar_color;
                if (is_null) {
                    bar_color = Color{0.0f, 0.9f, 1.0f, 0.7f};
                } else if (in_report) {
                    bar_color = passed ? Color{0.2f, 0.9f, 0.3f, 0.85f} : Color{1.0f, 0.3f, 0.15f, 0.85f};
                } else {
                    // Color by Z depth
                    bar_color = pos.z < 0.0f ? Color{0.3f, 0.85f, 1.0f, 0.8f} : Color{0.3f, 0.5f, 0.9f, 0.7f};
                }

                // Draw as a short horizontal bar to show X extent + Z height
                float bar_w = 16.0f;
                float bar_h = 5.0f;
                l.rect("sv_bar_" + std::to_string(sv_idx), RectParams{
                    .size = {bar_w, bar_h},
                    .pos = {screen.x - bar_w * 0.5f, screen.y - bar_h * 0.5f, 0.0f},
                    .fill = Fill{ .enabled = true, .solid = bar_color },
                    .stroke = { .enabled = true, .color = Color{bar_color.r, bar_color.g, bar_color.b, 0.5f}, .width = 0.5f }
                });

                // Vertical drop line from bar to Z=0 baseline (shows depth visually)
                if (!is_null && w_min_z < 0.0f && w_max_z > 0.0f) {
                    float z0_y = d_y0 + d_h - (0.0f - w_min_z) * s;
                    if (std::abs(screen.y - z0_y) > 3.0f) {
                        l.line("sv_drop_" + std::to_string(sv_idx), LineParams{
                            .from = {screen.x, screen.y, 0.0f},
                            .to = {screen.x, z0_y, 0.0f},
                            .thickness = 0.5f,
                            .color = Color{bar_color.r, bar_color.g, bar_color.b, 0.25f}
                        });
                    }
                }

                // Label (first 6 non-null layers)
                if (!is_null && sv_idx < 6) {
                    l.text("sv_lbl_" + std::to_string(sv_idx), TextParams{
                        .text = pair.first,
                        .pos = {screen.x + bar_w * 0.5f + 2.0f, screen.y - 3.0f, 0.0f},
                        .font_size = 7.0f,
                        .color = Color{0.7f, 0.9f, 0.8f, 0.55f}
                    });
                }
                sv_idx++;
            }

            // Draw camera Z position as white marker
            Vec2 cam_sv = to_sv(camera.position.x, camera.position.z);
            if (cam_sv.x >= sv_x + d_margin && cam_sv.x <= sv_x + sv_w - d_margin &&
                cam_sv.y >= d_y0 && cam_sv.y <= d_y0 + d_h) {
                l.circle("sv_cam", CircleParams{
                    .radius = 5.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.9f},
                    .pos = {cam_sv.x, cam_sv.y, 0.0f}
                });
                l.text("sv_cam_lbl", TextParams{
                    .text = "CAM",
                    .pos = {cam_sv.x + 7.0f, cam_sv.y - 4.0f, 0.0f},
                    .font_size = 7.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 0.8f}
                });
            }

            // Legend
            l.circle("sv_leg_pass", CircleParams{ .radius = 3.0f, .color = Color{0.2f, 0.9f, 0.3f, 0.85f}, .pos = {sv_x + 10.0f, sv_y + sv_h - 8.0f, 0.0f} });
            l.text("sv_leg_pass_t", TextParams{ .text = "pass", .pos = {sv_x + 16.0f, sv_y + sv_h - 12.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.7f, 0.65f, 0.5f} });
            l.circle("sv_leg_fail", CircleParams{ .radius = 3.0f, .color = Color{1.0f, 0.3f, 0.15f, 0.85f}, .pos = {sv_x + 50.0f, sv_y + sv_h - 8.0f, 0.0f} });
            l.text("sv_leg_fail_t", TextParams{ .text = "fail", .pos = {sv_x + 56.0f, sv_y + sv_h - 12.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.7f, 0.6f, 0.6f, 0.5f} });
            l.circle("sv_leg_cam", CircleParams{ .radius = 3.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.9f}, .pos = {sv_x + 88.0f, sv_y + sv_h - 8.0f, 0.0f} });
            l.text("sv_leg_cam_t", TextParams{ .text = "cam", .pos = {sv_x + 94.0f, sv_y + sv_h - 12.0f, 0.0f}, .font_size = 7.0f, .color = Color{0.6f, 0.7f, 0.65f, 0.5f} });
        }

        if (options.show_projected_bounds) {
            int idx = 0;
            for (const auto& lr : report.layers) {
                Color border_color = lr.passed ? Color{0.2f, 0.9f, 0.2f, 0.6f} : Color{1.0f, 0.2f, 0.2f, 0.8f};
                if (report.composition_name == "CameraFrustumCullingPrecisionTest") {
                    if (lr.bounds.visible_ratio > 0.0f) {
                        border_color = Color{0.2f, 0.9f, 0.2f, 0.6f}; // Green inside frustum
                    } else {
                        border_color = Color{0.5f, 0.5f, 0.5f, 0.15f}; // Gray/Transparent outside frustum
                    }
                }
                
                Vec2 b_size = lr.bounds.max - lr.bounds.min;
                if (b_size.x > 0.0f && b_size.y > 0.0f) {
                    std::string node_name = "bounds_hud_" + lr.name + "_" + std::to_string(idx);
                    l.rect(node_name, RectParams{
                        .size = b_size,
                        .pos = {lr.bounds.min.x, lr.bounds.min.y, 0.0f},
                        .fill = Fill{ .enabled = false },
                        .stroke = { .enabled = true, .color = border_color, .width = 1.5f }
                    });

                    if (options.show_layer_names) {
                        std::string label_name = "label_hud_" + lr.name + "_" + std::to_string(idx);
                        l.text(label_name, TextParams{
                            .text = lr.name + (lr.passed ? " (PASS)" : " (FAIL)"),
                            .pos = {lr.bounds.min.x + 5.0f, lr.bounds.min.y + 15.0f, 0.0f},
                            .font_size = 12.0f,
                            .color = border_color
                        });
                    }
                }
                idx++;
            }
        }
    });
}

} // namespace chronon3d
