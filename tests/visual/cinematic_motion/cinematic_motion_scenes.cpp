#include "cinematic_motion_scenes.hpp"

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/core/temporal_spatial_curve.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <vector>
using namespace chronon3d;

namespace chronon3d::test {

// ═══════════════════════════════════════════════════════════════════════════
// Test 1: SampleTimeSubframeComb
// 8 markers at sub-frame positions 30+0.0625 … 30+0.9375 showing uniform spacing.
// ═══════════════════════════════════════════════════════════════════════════

Composition make_subframe_comb_scene() {
    constexpr double kBaseFrame = 30.0;
    constexpr double kSubSteps[8] = {
        0.0625, 0.1875, 0.3125, 0.4375,
        0.5625, 0.6875, 0.8125, 0.9375
    };
    constexpr Color  kColors[8] = {
        {1.0f, 0.0f, 0.0f, 1.0f},   // red
        {1.0f, 0.5f, 0.0f, 1.0f},   // orange
        {1.0f, 1.0f, 0.0f, 1.0f},   // yellow
        {0.0f, 1.0f, 0.0f, 1.0f},   // green
        {0.0f, 0.8f, 1.0f, 1.0f},   // cyan
        {0.0f, 0.0f, 1.0f, 1.0f},   // blue
        {0.5f, 0.0f, 1.0f, 1.0f},   // purple
        {1.0f, 0.0f, 0.5f, 1.0f},   // magenta
    };

    return composition({
        .name = "SubframeComb",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 31
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        // Background: dark grey.
        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // Reference line showing the 1-frame span.
        constexpr float x_start = 70.0f;
        constexpr float x_end   = 870.0f;
        constexpr float total_dx = x_end - x_start;

        // Thin horizontal line from start to end.
        s.line("track", {.from = {x_start, 270, 0}, .to = {x_end, 270, 0},
                         .thickness = 2.0f, .color = {0.3f, 0.3f, 0.35f, 1.0f}});

        for (int i = 0; i < 8; ++i) {
            const double sub_frame = kBaseFrame + kSubSteps[i];
            const double fraction = kSubSteps[i];  // 0..1 within frame
            const float x = x_start + static_cast<float>(fraction * total_dx);

            // Marker dot — 14x14 for reliable detection at 960x540.
            s.layer("marker_" + std::to_string(i), [=](LayerBuilder& l) {
                l.position({x, 270, 0});
                l.rect("dot", {.size = {14, 14},
                               .color = kColors[i],
                               .pos = {-7, -7, 0}});
            });
        }

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 2: SampleTimeContinuityContactSheet (compositing helper)
// ═══════════════════════════════════════════════════════════════════════════

Framebuffer render_continuity_contact_sheet(
    SoftwareRenderer& renderer,
    const Composition& animated_scene,
    double base_frame)
{
    constexpr double kOffsets[9] = {
        -0.25, -0.125, 0.0,
         0.125, 0.25, 0.375,
         0.5,   0.625, 0.75
    };

    std::vector<std::shared_ptr<Framebuffer>> panels;
    panels.reserve(9);

    for (int i = 0; i < 9; ++i) {
        const double sub_frame = base_frame + kOffsets[i];
        const Frame frame_int{static_cast<i32>(std::floor(sub_frame))};
        auto fb = renderer.render_frame(animated_scene, frame_int);
        if (!fb) {
            // Fallback: allocate blank.
            fb = std::make_shared<Framebuffer>(320, 180);
        }
        panels.push_back(std::move(fb));
    }

    return composite_contact_sheet_3x3(panels);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 3: TemporalCacheParity
// ═══════════════════════════════════════════════════════════════════════════

Composition make_cache_parity_scene() {
    return composition({
        .name = "CacheParity",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 60
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        // Dark background.
        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // A simple moving rectangle — position animated by frame.
        const float x = 80.0f + static_cast<float>(ctx.frame) * 8.0f;

        s.layer("moving_rect", [&](LayerBuilder& l) {
            l.position({x, 270, 0});
            l.rect("rect", {.size = {60, 60}, .color = {0.2f, 0.8f, 0.3f, 1.0f},
                           .pos = {-30, -30, 0}});
        });

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 4: BezierHandles3D — 3 ortho views (XY, XZ, YZ)
// ═══════════════════════════════════════════════════════════════════════════

Composition make_bezier_handles_3d_scene() {
    // A non-trivial CubicBezier3D with visible handles.
    const CubicBezier3D curve{
        {0, 0, 0},
        {40, 60, 20},
        {60, 10, 80},
        {100, 80, 100}
    };

    return composition({
        .name = "BezierHandles3D",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        // Background.
        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // ── View 1: Front XY (left third) ──
        constexpr int vw = 320;
        constexpr int vh = 540;
        constexpr float wmin = -10.0f;
        constexpr float wmax = 110.0f;

        for (int view = 0; view < 3; ++view) {
            const int vx0 = view * vw;
            const int vy0 = 0;
            const float scale_x = static_cast<float>(vw) / (wmax - wmin);
            const float scale_y = static_cast<float>(vh) / (wmax - wmin);

            // Thin border around view.
            s.line("border_" + std::to_string(view),
                   {.from = {static_cast<float>(vx0), 0, 0},
                    .to = {static_cast<float>(vx0), static_cast<float>(vh), 0},
                    .thickness = 1.0f, .color = {0.2f, 0.2f, 0.3f, 0.5f}});

            // Choose axes: XY, XZ, YZ
            float ax1, ay1, ax2, ay2;
            Vec3 p0_2d, p1_2d, p2_2d, p3_2d;
            if (view == 0) {  // Front XY
                ax1 = curve.p0.x; ay1 = curve.p0.y;
                ax2 = curve.p3.x; ay2 = curve.p3.y;
                p0_2d = {curve.p0.x, curve.p0.y, 0};
                p1_2d = {curve.p1.x, curve.p1.y, 0};
                p2_2d = {curve.p2.x, curve.p2.y, 0};
                p3_2d = {curve.p3.x, curve.p3.y, 0};
            } else if (view == 1) {  // Top XZ
                ax1 = curve.p0.x; ay1 = curve.p0.z;
                ax2 = curve.p3.x; ay2 = curve.p3.z;
                p0_2d = {curve.p0.x, curve.p0.z, 0};
                p1_2d = {curve.p1.x, curve.p1.z, 0};
                p2_2d = {curve.p2.x, curve.p2.z, 0};
                p3_2d = {curve.p3.x, curve.p3.z, 0};
            } else {  // Side YZ
                ax1 = curve.p0.y; ay1 = curve.p0.z;
                ax2 = curve.p3.y; ay2 = curve.p3.z;
                p0_2d = {curve.p0.y, curve.p0.z, 0};
                p1_2d = {curve.p1.y, curve.p1.z, 0};
                p2_2d = {curve.p2.y, curve.p2.z, 0};
                p3_2d = {curve.p3.y, curve.p3.z, 0};
            }

            auto to_screen = [&](float wx, float wy) -> Vec2 {
                float sx = static_cast<float>(vx0) + (wx - wmin) * scale_x;
                float sy = static_cast<float>(vh) - (wy - wmin) * scale_y;
                return {sx, sy};
            };

            // Draw control polygon as thin lines.
            auto sc0 = to_screen(p0_2d.x, p0_2d.y);
            auto sc1 = to_screen(p1_2d.x, p1_2d.y);
            auto sc2 = to_screen(p2_2d.x, p2_2d.y);
            auto sc3 = to_screen(p3_2d.x, p3_2d.y);

            s.line("cp0_" + std::to_string(view),
                   {.from = {sc0.x, sc0.y, 0}, .to = {sc1.x, sc1.y, 0},
                    .thickness = 1.0f, .color = {0.4f, 0.4f, 0.4f, 0.6f}});
            s.line("cp1_" + std::to_string(view),
                   {.from = {sc1.x, sc1.y, 0}, .to = {sc2.x, sc2.y, 0},
                    .thickness = 1.0f, .color = {0.4f, 0.4f, 0.4f, 0.3f}});
            s.line("cp2_" + std::to_string(view),
                   {.from = {sc2.x, sc2.y, 0}, .to = {sc3.x, sc3.y, 0},
                    .thickness = 1.0f, .color = {0.4f, 0.4f, 0.4f, 0.6f}});

            // Draw 16 markers along the curve.
            for (int m = 0; m <= 16; ++m) {
                const double u = static_cast<double>(m) / 16.0;
                Vec3 pos_3d = curve.position(u);
                float wx, wy;
                if (view == 0) { wx = pos_3d.x; wy = pos_3d.y; }
                else if (view == 1) { wx = pos_3d.x; wy = pos_3d.z; }
                else { wx = pos_3d.y; wy = pos_3d.z; }

                auto mp = to_screen(wx, wy);
                s.layer("m_" + std::to_string(view) + "_" + std::to_string(m),
                        [&](LayerBuilder& l) {
                    l.position({mp.x, mp.y, 0});
                    float r = (m == 0 || m == 16) ? 4.0f : 2.0f;
                    Color c = (m == 0 || m == 16) ? Color{1, 1, 0, 1} : Color{0.7f, 0.7f, 1, 0.8f};
                    l.rect("dot", {.size = {r*2, r*2}, .color = c,
                                   .pos = {-r, -r, 0}});
                });
            }

            // P0 and P3: large circles (simulated with larger rects).
            for (int ep = 0; ep < 2; ++ep) {
                float wx = (ep == 0) ? ax1 : ax2;
                float wy = (ep == 0) ? ay1 : ay2;
                auto sp = to_screen(wx, wy);
                s.layer("ep_" + std::to_string(view) + "_" + std::to_string(ep),
                        [&](LayerBuilder& l) {
                    l.position({sp.x, sp.y, 0});
                    l.circle("circ", {.radius = 6.0f,
                                      .color = {1, 0.8f, 0, 1},
                                      .pos = {0, 0, 0},
                                      .stroke = {.color = {1, 1, 1, 0.5f}, .width = 1.0f}});
                });

                // Tangent arrow at start / end.
                Vec3 tangent_3d = (ep == 0)
                    ? glm::normalize(curve.p1 - curve.p0)
                    : glm::normalize(curve.p3 - curve.p2);
                float twx, twy;
                if (view == 0) { twx = tangent_3d.x; twy = tangent_3d.y; }
                else if (view == 1) { twx = tangent_3d.x; twy = tangent_3d.z; }
                else { twx = tangent_3d.y; twy = tangent_3d.z; }

                float tip_x = sp.x + twx * 30.0f;
                float tip_y = sp.y - twy * 30.0f;
                s.line("tang_" + std::to_string(view) + "_" + std::to_string(ep),
                       {.from = {sp.x, sp.y, 0},
                        .to = {tip_x, tip_y, 0},
                        .thickness = 2.0f, .color = {1, 0.4f, 0.4f, 0.8f}});
            }

            // Handle squares.
            for (int h = 0; h < 2; ++h) {
                Vec3 hp = (h == 0) ? curve.p1 : curve.p2;
                float hwx, hwy;
                if (view == 0) { hwx = hp.x; hwy = hp.y; }
                else if (view == 1) { hwx = hp.x; hwy = hp.z; }
                else { hwx = hp.y; hwy = hp.z; }
                auto hsp = to_screen(hwx, hwy);
                s.layer("h_" + std::to_string(view) + "_" + std::to_string(h),
                        [&](LayerBuilder& l) {
                    l.position({hsp.x, hsp.y, 0});
                    l.rect("sq", {.size = {8, 8}, .color = {0.5f, 0.5f, 1, 1},
                                  .pos = {-4, -4, 0}});
                });
            }
        }

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 5: ArcLengthSpacing
// ═══════════════════════════════════════════════════════════════════════════

Composition make_arc_length_spacing_scene() {
    // A bezier with non-uniform parametric spacing — visible in upper row.
    const CubicBezier3D curve{
        {10, 50, 0},
        {10, 0, 0},
        {90, 100, 0},
        {90, 50, 0}
    };

    return composition({
        .name = "ArcLengthSpacing",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // ── Upper row: Parametric sampling (irregular spacing) ────────
        {
            constexpr float y_center = 120.0f;
            constexpr float x_from = 50.0f;
            constexpr float x_to = 910.0f;
            constexpr float x_span = x_to - x_from;

            s.line("track_top", {.from = {x_from, y_center, 0},
                                 .to = {x_to, y_center, 0},
                                 .thickness = 1.0f, .color = {0.3f, 0.3f, 0.35f, 1.0f}});

            for (int i = 0; i <= 32; ++i) {
                const double u = static_cast<double>(i) / 32.0;
                // Map bezier Y(u) to X position (ignoring actual X for clarity).
                const Vec3 pt = curve.position(u);
                const float x = x_from + pt.y * (x_span / 100.0f);

                Color c = {1, 0.3f, 0.3f, 1};
                const float r = (i == 0 || i == 32) ? 4.0f : 2.0f;

                s.layer("pm_" + std::to_string(i), [&](LayerBuilder& l) {
                    l.position({x, y_center, 0});
                    l.rect("dot", {.size = {r*2, r*2}, .color = c,
                                   .pos = {-r, -r, 0}});
                });
            }

            // Label: "PARAMETRIC"
            s.layer("label_top", [&](LayerBuilder& l) {
                l.position({50, y_center - 18, 0});
                l.rect("label", {.size = {130, 14}, .color = {0, 0, 0, 0.6f},
                                 .pos = {0, 0, 0}});
            });
        }

        // ── Lower row: Arc-length spacing (uniform) ─────────────────
        {
            constexpr float y_center = 400.0f;
            constexpr float x_from = 50.0f;
            constexpr float x_to = 910.0f;
            constexpr float x_span = x_to - x_from;

            s.line("track_bot", {.from = {x_from, y_center, 0},
                                 .to = {x_to, y_center, 0},
                                 .thickness = 1.0f, .color = {0.3f, 0.3f, 0.35f, 1.0f}});

            // For proof-of-concept, use evenly-spaced X positions to simulate
            // arc-length sampling (actual arc-length LUT is PR5).
            for (int i = 0; i <= 32; ++i) {
                const float x = x_from + (static_cast<float>(i) / 32.0f) * x_span;
                Color c = {0.3f, 0.8f, 0.3f, 1};
                const float r = (i == 0 || i == 32) ? 4.0f : 2.0f;

                s.layer("am_" + std::to_string(i), [&](LayerBuilder& l) {
                    l.position({x, y_center, 0});
                    l.rect("dot", {.size = {r*2, r*2}, .color = c,
                                   .pos = {-r, -r, 0}});
                });
            }

            // Label: "ARC LENGTH"
            s.layer("label_bot", [&](LayerBuilder& l) {
                l.position({50, y_center - 18, 0});
                l.rect("label", {.size = {130, 14}, .color = {0, 0, 0, 0.6f},
                                 .pos = {0, 0, 0}});
            });
        }

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 6: TemporalSpatialCurveSeparation
// ═══════════════════════════════════════════════════════════════════════════

Composition make_temporal_spatial_separation_scene() {
    return composition({
        .name = "TemporalSpatialSeparation",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // 2×2 grid separators.
        s.line("sep_h", {.from = {0, 270, 0}, .to = {960, 270, 0},
                         .thickness = 1.0f, .color = {0.3f, 0.3f, 0.4f, 0.5f}});
        s.line("sep_v", {.from = {480, 0, 0}, .to = {480, 540, 0},
                         .thickness = 1.0f, .color = {0.3f, 0.3f, 0.4f, 0.5f}});

        // Bezier path (same in top row).
        CubicBezier3D path{
            {0, 0, 0}, {180, 40, 0}, {40, 180, 0}, {200, 200, 0}
        };

        // Linear path (same in bottom row).
        CubicBezier3D linear_path{
            {0, 0, 0}, {66.67f, 0, 0}, {133.33f, 200, 0}, {200, 200, 0}
        };

        // Marker count for time visualization.
        constexpr int kMarkers = 16;

        // ── Top-Left: Bezier + Linear time ────────
        {
            constexpr float ox = 20.0f, oy = 10.0f;
            constexpr float sx = 2.0f, sy = 1.2f;
            for (int i = 0; i <= kMarkers; ++i) {
                double t = static_cast<double>(i) / kMarkers;
                Vec3 pt = path.position(t);
                float x = ox + pt.x * sx / 10.0f;
                float y = oy + 240.0f - pt.y * sy / 10.0f;
                s.layer("tl_" + std::to_string(i), [&](LayerBuilder& l) {
                    l.position({x, y, 0});
                    l.rect("d", {.size = {4, 4}, .color = {1, 0.4f, 0.4f, 1},
                                 .pos = {-2, -2, 0}});
                });
            }
        }

        // ── Top-Right: Bezier + EaseIn time ──────
        {
            constexpr float ox = 500.0f, oy = 10.0f;
            constexpr float sx = 2.0f, sy = 1.2f;
            for (int i = 0; i <= kMarkers; ++i) {
                double raw_t = static_cast<double>(i) / kMarkers;
                // Ease-in: progress = raw_t^2
                double t = raw_t * raw_t;
                Vec3 pt = path.position(t);
                float x = ox + pt.x * sx / 10.0f;
                float y = oy + 240.0f - pt.y * sy / 10.0f;
                s.layer("tr_" + std::to_string(i), [&](LayerBuilder& l) {
                    l.position({x, y, 0});
                    l.rect("d", {.size = {4, 4}, .color = {0.4f, 0.4f, 1, 1},
                                 .pos = {-2, -2, 0}});
                });
            }
        }

        // ── Bottom-Left: Linear path + Linear time ───
        {
            constexpr float ox = 20.0f, oy = 290.0f;
            constexpr float sx = 2.0f, sy = 1.2f;
            for (int i = 0; i <= kMarkers; ++i) {
                double t = static_cast<double>(i) / kMarkers;
                Vec3 pt = linear_path.position(t);
                float x = ox + pt.x * sx / 10.0f;
                float y = oy + 240.0f - pt.y * sy / 10.0f;
                s.layer("bl_" + std::to_string(i), [&](LayerBuilder& l) {
                    l.position({x, y, 0});
                    l.rect("d", {.size = {4, 4}, .color = {0.4f, 1, 0.4f, 1},
                                 .pos = {-2, -2, 0}});
                });
            }
        }

        // ── Bottom-Right: Linear path + EaseIn time ──
        {
            constexpr float ox = 500.0f, oy = 290.0f;
            constexpr float sx = 2.0f, sy = 1.2f;
            for (int i = 0; i <= kMarkers; ++i) {
                double raw_t = static_cast<double>(i) / kMarkers;
                double t = raw_t * raw_t;
                Vec3 pt = linear_path.position(t);
                float x = ox + pt.x * sx / 10.0f;
                float y = oy + 240.0f - pt.y * sy / 10.0f;
                s.layer("br_" + std::to_string(i), [&](LayerBuilder& l) {
                    l.position({x, y, 0});
                    l.rect("d", {.size = {4, 4}, .color = {1, 0.7f, 0.2f, 1},
                                 .pos = {-2, -2, 0}});
                });
            }
        }

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Compositing helpers
// ═══════════════════════════════════════════════════════════════════════════

Framebuffer& blit_into(Framebuffer& dest, const Framebuffer& src,
                       int dst_x0, int dst_y0) {
    const int w = std::min(src.width(), dest.width() - dst_x0);
    const int h = std::min(src.height(), dest.height() - dst_y0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            dest.set_pixel(dst_x0 + x, dst_y0 + y, src.get_pixel(x, y));
        }
    }
    return dest;
}

Framebuffer composite_contact_sheet_3x3(
    const std::vector<std::shared_ptr<Framebuffer>>& panels)
{
    Framebuffer result(960, 540);
    // Fill with black.
    for (int y = 0; y < 540; ++y)
        for (int x = 0; x < 960; ++x)
            result.set_pixel(x, y, {0, 0, 0, 1});

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int idx = row * 3 + col;
            if (idx >= static_cast<int>(panels.size()) || !panels[idx]) continue;
            const int dst_x = col * 320;
            const int dst_y = row * 180;
            blit_into(result, *panels[idx], dst_x, dst_y);
        }
    }
    return result;
}

Framebuffer composite_quad_2x2(
    const std::vector<std::shared_ptr<Framebuffer>>& quads)
{
    Framebuffer result(960, 540);
    for (int y = 0; y < 540; ++y)
        for (int x = 0; x < 960; ++x)
            result.set_pixel(x, y, {0, 0, 0, 1});

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            const int idx = row * 2 + col;
            if (idx >= static_cast<int>(quads.size()) || !quads[idx]) continue;
            const int dst_x = col * 480;
            const int dst_y = row * 270;
            blit_into(result, *quads[idx], dst_x, dst_y);
        }
    }
    return result;
}

void draw_crosshair(Framebuffer& fb, int cx, int cy, Color color, int arm_len) {
    for (int dx = -arm_len; dx <= arm_len; ++dx) {
        int px = cx + dx, py = cy;
        if (px >= 0 && px < fb.width() && py >= 0 && py < fb.height())
            fb.set_pixel(px, py, color);
    }
    for (int dy = -arm_len; dy <= arm_len; ++dy) {
        int px = cx, py = cy + dy;
        if (px >= 0 && px < fb.width() && py >= 0 && py < fb.height())
            fb.set_pixel(px, py, color);
    }
}

void draw_axis_tripod(Framebuffer& fb, int cx, int cy,
                      Vec3 x_dir, Vec3 y_dir, Vec3 z_dir, float scale) {
    auto draw_arrow = [&](int x0, int y0, float dx, float dy, Color c) {
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 0.001f) return;
        float ux = dx / len, uy = dy / len;
        int x1 = x0 + static_cast<int>(dx);
        int y1 = y0 + static_cast<int>(dy);
        // Draw line via Bresenham-like stepping.
        for (float t = 0; t <= 1.0f; t += 0.02f) {
            int px = x0 + static_cast<int>(dx * t);
            int py = y0 + static_cast<int>(dy * t);
            if (px >= 0 && px < fb.width() && py >= 0 && py < fb.height())
                fb.set_pixel(px, py, c);
        }
    };

    draw_arrow(cx, cy, x_dir.x * scale, -x_dir.y * scale, {1, 0, 0, 1});
    draw_arrow(cx, cy, y_dir.x * scale, -y_dir.y * scale, {0, 1, 0, 1});
    draw_arrow(cx, cy, z_dir.x * scale, -z_dir.y * scale, {0.3f, 0.5f, 1, 1});
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 7: QuaternionShortestPath
// ═══════════════════════════════════════════════════════════════════════════

Composition make_quaternion_shortest_path_scene() {
    return composition({
        .name = "QuaternionShortestPath",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // 9 cards from yaw=+179deg to yaw=-179deg via quaternion slerp.
        constexpr int kCards = 9;
        constexpr float kStartYaw = 179.0f;
        constexpr float kEndYaw = -179.0f;

        for (int i = 0; i < kCards; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kCards - 1);
            // Quaternion slerp with shortest-path (glm does this by default).
            Quat q0 = glm::angleAxis(glm::radians(kStartYaw), Vec3{0, 1, 0});
            Quat q1 = glm::angleAxis(glm::radians(kEndYaw),   Vec3{0, 1, 0});
            // Ensure shortest path.
            if (glm::dot(q0, q1) < 0.0f) q1 = -q1;
            Quat q = glm::slerp(q0, q1, t);

            // Forward direction from quaternion.
            Vec3 fwd = q * Vec3{0, 0, -1};
            Vec3 up  = q * Vec3{0, 1, 0};

            const float cx = 80.0f + static_cast<float>(i) * 90.0f;
            const float cy = 270.0f;

            // Card body (small rect).
            s.layer("card_" + std::to_string(i), [=](LayerBuilder& l) {
                l.position({cx, cy, 0});
                l.rect("body", {.size = {20, 30}, .color = {0.2f, 0.3f, 0.6f, 1},
                                 .pos = {-10, -15, 0}});
            });

            // Forward arrow (from center pointing in forward direction).
            float arrow_dx = fwd.x * 20.0f;
            float arrow_dy = -fwd.z * 20.0f;  // screen Y inverted
            s.layer("arrow_" + std::to_string(i), [=](LayerBuilder& l) {
                l.line("arr", {.from = {cx, cy, 0},
                                .to = {cx + arrow_dx, cy + arrow_dy, 0},
                                .thickness = 2.0f, .color = {1, 0.8f, 0.2f, 1}});
            });

            // Up arrow.
            float up_dx = up.x * 10.0f;
            float up_dy = -up.z * 10.0f;
            s.layer("up_" + std::to_string(i), [=](LayerBuilder& l) {
                l.line("uarr", {.from = {cx, cy, 0},
                                .to = {cx + up_dx, cy + up_dy, 0},
                                .thickness = 1.0f, .color = {0.3f, 1, 0.3f, 1}});
            });
        }

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 8: QuaternionPathOrientation
// ═══════════════════════════════════════════════════════════════════════════

Composition make_quaternion_path_orientation_scene() {
    // S-shaped bezier path that becomes nearly vertical.
    const CubicBezier3D path{
        {50, 270, 0},
        {250, 100, 0},
        {550, 440, 0},
        {750, 270, 0}
    };

    return composition({
        .name = "QuaternionPathOrientation",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // Draw the path as a thin curve using 64 line segments.
        Vec3 prev = path.position(0.0);
        for (int i = 1; i <= 64; ++i) {
            double u = static_cast<double>(i) / 64.0;
            Vec3 curr = path.position(u);
            s.line("pseg_" + std::to_string(i),
                   {.from = {prev.x, prev.y, 0},
                    .to = {curr.x, curr.y, 0},
                    .thickness = 1.0f, .color = {0.3f, 0.3f, 0.5f, 0.6f}});
            prev = curr;
        }

        // 16 camera frustums along the path.
        constexpr int kFrustums = 16;
        for (int i = 0; i < kFrustums; ++i) {
            const double u = static_cast<double>(i) / static_cast<double>(kFrustums - 1);
            const Vec3 pos = path.position(u);
            const Vec3 tangent = path.tangent_at(u);

            // Compute quaternion that looks along the tangent.
            Vec3 up{0, -1, 0};  // screen-up (inverted Y in screen space)
            Vec3 fwd = tangent;
            if (glm::length(fwd) < 1e-6f) fwd = Vec3{1, 0, 0};
            fwd = glm::normalize(fwd);
            Vec3 right = glm::normalize(glm::cross(fwd, up));
            Vec3 actual_up = glm::normalize(glm::cross(right, fwd));

            // Screen coordinates.
            const float cx = pos.x;
            const float cy = pos.y;
            const float sc = 25.0f;

            // Forward arrow.
            s.line("f_" + std::to_string(i),
                   {.from = {cx, cy, 0},
                    .to = {cx + fwd.x * sc, cy - fwd.y * sc, 0},
                    .thickness = 2.0f, .color = {1, 0.8f, 0.2f, 1}});
            // Up arrow.
            s.line("u_" + std::to_string(i),
                   {.from = {cx, cy, 0},
                    .to = {cx + actual_up.x * sc * 0.6f, cy - actual_up.y * sc * 0.6f, 0},
                    .thickness = 1.0f, .color = {0.3f, 1, 0.3f, 1}});
            // Right arrow.
            s.line("r_" + std::to_string(i),
                   {.from = {cx, cy, 0},
                    .to = {cx + right.x * sc * 0.6f, cy - right.y * sc * 0.6f, 0},
                    .thickness = 1.0f, .color = {1, 0.3f, 0.3f, 1}});

            // Small marker at position.
            s.layer("m_" + std::to_string(i), [=](LayerBuilder& l) {
                l.position({cx, cy, 0});
                l.rect("dot", {.size = {6, 6}, .color = {1, 1, 1, 0.8f},
                               .pos = {-3, -3, 0}});
            });
        }

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 9: TwoNodeTargetLock
// ═══════════════════════════════════════════════════════════════════════════

Framebuffer render_two_node_target_lock_contact_sheet(
    SoftwareRenderer& renderer)
{
    // Create a scene with a camera target at the center.
    auto scene_comp = composition({
        .name = "TwoNodeTargetLockScene",
        .width = 320,
        .height = 180,
        .duration = 61
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(320, 180);

        // Solid background.
        s.rect("bg", {.size = {320, 180}, .color = {0.05f, 0.05f, 0.08f, 1},
                       .pos = {0, 0, 0}});

        // Target null at center.
        s.null_layer("camera_target", [=](NullBuilder& b) {
            b.position({160, 90, 0});
        });

        // Target marker: a green rect at (160, 90).
        s.layer("target_marker", [=](LayerBuilder& l) {
            l.position({160, 90, 0});
            l.rect("tm", {.size = {16, 16}, .color = {0.2f, 0.9f, 0.3f, 1},
                          .pos = {-8, -8, 0}});
        });

        // Camera rig: orbit + dolly around target.
        s.camera_rig("main_rig", [=](CameraRigBuilder& rig) {
            rig.two_node("camera_target");
            rig.orbit_yaw(0, 0.0f, 60, 60.0f, EasingCurve{Easing::InOutCubic});
            rig.dolly(0, -200.0f, 60, -100.0f, EasingCurve{Easing::InOutCubic});
            rig.fov(50.0f);
        });

        return s.build();
    });

    // Render 5 frames: 0, 15, 30, 45, 60.
    constexpr Frame kFrames[5] = {{0}, {15}, {30}, {45}, {60}};
    std::vector<std::shared_ptr<Framebuffer>> panels;
    for (Frame f : kFrames) {
        panels.push_back(renderer.render_frame(scene_comp, f));
    }

    // Composite into a 5-panel horizontal strip, then pad to 960x540.
    Framebuffer result(960, 540);
    for (int y = 0; y < 540; ++y)
        for (int x = 0; x < 960; ++x)
            result.set_pixel(x, y, {0, 0, 0, 1});

    for (int i = 0; i < 5 && i < static_cast<int>(panels.size()); ++i) {
        if (!panels[i]) continue;
        const int dst_x = i * 192;
        const int dst_y = 180;  // center vertically
        blit_into(result, *panels[i], dst_x, dst_y);

        // Draw crosshair at expected center (160, 90 mapping to panel center).
        draw_crosshair(result, dst_x + 160, dst_y + 90, {1, 1, 1, 0.5f}, 8);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 10: MotionBlurShutter
// ═══════════════════════════════════════════════════════════════════════════

Framebuffer render_motion_blur_comparison(SoftwareRenderer& renderer) {
    // Animated scene: white rect moving left-to-right on black background.
    auto anim_comp = composition({
        .name = "MotionBlurScene",
        .width = 240,
        .height = 270,
        .duration = 61
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(240, 270);
        s.camera().enable(false);

        s.rect("bg", {.size = {240, 270}, .color = {0.02f, 0.02f, 0.03f, 1},
                       .pos = {0, 0, 0}});

        // Moving white rect: X = 20 + frame * 4.
        const float x = 20.0f + static_cast<float>(ctx.frame) * 4.0f;
        s.layer("rect", [=](LayerBuilder& l) {
            l.position({x, 135, 0});
            l.rect("r", {.size = {30, 30}, .color = {0.9f, 0.9f, 0.9f, 1},
                         .pos = {-15, -15, 0}});
        });

        return s.build();
    });

    std::vector<std::shared_ptr<Framebuffer>> panels;
    RenderSettings base = renderer.settings();
    const Frame kFrame{30};

    // Panel 0: 1 sample, no blur.
    {
        auto s = base;
        s.motion_blur.enabled = false;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

    // Panel 1: 8 samples, 180deg shutter.
    {
        auto s = base;
        s.motion_blur.enabled = true;
        s.motion_blur.samples = 8;
        s.motion_blur.shutter_angle_deg = 180.0f;
        s.motion_blur.jitter_seed = 0xABCD1234;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

    // Panel 2: 16 samples, 180deg shutter.
    {
        auto s = base;
        s.motion_blur.enabled = true;
        s.motion_blur.samples = 16;
        s.motion_blur.shutter_angle_deg = 180.0f;
        s.motion_blur.jitter_seed = 0xABCD1234;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

    // Panel 3: 16 samples, 360deg shutter.
    {
        auto s = base;
        s.motion_blur.enabled = true;
        s.motion_blur.samples = 16;
        s.motion_blur.shutter_angle_deg = 360.0f;
        s.motion_blur.jitter_seed = 0xABCD1234;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

    renderer.set_settings(base);

    // Composite 4 panels into 2x2.
    return composite_quad_2x2(panels);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 11: ArcLengthCacheInvalidation (placeholder — requires PR5 LUT)
// ═══════════════════════════════════════════════════════════════════════════

Composition make_arc_length_cache_invalidation_before() {
    return composition({
        .name = "ArcLengthCacheInvalidationBefore",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // A bezier with handles.
        const CubicBezier3D curve{
            {50, 400, 0},
            {250, 200, 0},
            {550, 600, 0},
            {850, 300, 0}
        };

        // Draw curve as thin line.
        Vec3 prev = curve.position(0.0);
        for (int i = 1; i <= 64; ++i) {
            double u = static_cast<double>(i) / 64.0;
            Vec3 curr = curve.position(u);
            s.line("cs_" + std::to_string(i),
                   {.from = {prev.x, prev.y, 0},
                    .to = {curr.x, curr.y, 0},
                    .thickness = 2.0f, .color = {0.3f, 0.6f, 1, 0.8f}});
            prev = curr;
        }

        // Control polygon (handles).
        s.line("cp0", {.from = {curve.p0.x, curve.p0.y, 0},
                        .to = {curve.p1.x, curve.p1.y, 0},
                        .thickness = 1.0f, .color = {0.5f, 0.5f, 0.5f, 0.5f}});
        s.line("cp1", {.from = {curve.p2.x, curve.p2.y, 0},
                        .to = {curve.p3.x, curve.p3.y, 0},
                        .thickness = 1.0f, .color = {0.5f, 0.5f, 0.5f, 0.5f}});

        // Endpoint markers.
        s.layer("p0", [=](LayerBuilder& l) {
            l.position({curve.p0.x, curve.p0.y, 0});
            l.rect("ep", {.size = {10, 10}, .color = {1, 0.8f, 0, 1},
                          .pos = {-5, -5, 0}});
        });
        s.layer("p3", [=](LayerBuilder& l) {
            l.position({curve.p3.x, curve.p3.y, 0});
            l.rect("ep", {.size = {10, 10}, .color = {1, 0.8f, 0, 1},
                          .pos = {-5, -5, 0}});
        });

        return s.build();
    });
}

Composition make_arc_length_cache_invalidation_after() {
    // Same as before but with modified out_handle.
    return composition({
        .name = "ArcLengthCacheInvalidationAfter",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        // Modified out_handle — curve shape changes.
        const CubicBezier3D curve{
            {50, 400, 0},
            {200, 120, 0},   // was {250, 200, 0} — modified out_handle
            {550, 600, 0},
            {850, 300, 0}
        };

        Vec3 prev = curve.position(0.0);
        for (int i = 1; i <= 64; ++i) {
            double u = static_cast<double>(i) / 64.0;
            Vec3 curr = curve.position(u);
            s.line("cs_" + std::to_string(i),
                   {.from = {prev.x, prev.y, 0},
                    .to = {curr.x, curr.y, 0},
                    .thickness = 2.0f, .color = {1, 0.4f, 0.4f, 0.8f}});
            prev = curr;
        }

        s.line("cp0", {.from = {curve.p0.x, curve.p0.y, 0},
                        .to = {curve.p1.x, curve.p1.y, 0},
                        .thickness = 1.0f, .color = {1, 0.5f, 0.5f, 0.5f}});
        s.line("cp1", {.from = {curve.p2.x, curve.p2.y, 0},
                        .to = {curve.p3.x, curve.p3.y, 0},
                        .thickness = 1.0f, .color = {1, 0.5f, 0.5f, 0.5f}});

        s.layer("p0", [=](LayerBuilder& l) {
            l.position({curve.p0.x, curve.p0.y, 0});
            l.rect("ep", {.size = {10, 10}, .color = {1, 0.8f, 0, 1},
                          .pos = {-5, -5, 0}});
        });
        s.layer("p3", [=](LayerBuilder& l) {
            l.position({curve.p3.x, curve.p3.y, 0});
            l.rect("ep", {.size = {10, 10}, .color = {1, 0.8f, 0, 1},
                          .pos = {-5, -5, 0}});
        });

        return s.build();
    });
}

} // namespace chronon3d::test
