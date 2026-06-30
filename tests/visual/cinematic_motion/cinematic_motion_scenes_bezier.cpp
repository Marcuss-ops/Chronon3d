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
// ═══════════════════════════════════════════════════════════════════════════

Composition make_subframe_comb_scene() {
    constexpr double kBaseFrame = 30.0;
    constexpr double kSubSteps[8] = {
        0.0625, 0.1875, 0.3125, 0.4375,
        0.5625, 0.6875, 0.8125, 0.9375
    };
    constexpr Color  kColors[8] = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 0.5f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.8f, 1.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {0.5f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 0.5f, 1.0f},
    };

    return composition({
        .name = "SubframeComb",
        .width = kCinematicWidth,
        .height = kCinematicHeight,
        .duration = 31
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(kCinematicWidth, kCinematicHeight);
        s.camera().enable(false);

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        constexpr float x_start = 70.0f;
        constexpr float x_end   = 870.0f;
        constexpr float total_dx = x_end - x_start;

        s.line("track", {.from = {x_start, 270, 0}, .to = {x_end, 270, 0},
                         .thickness = 2.0f, .color = {0.3f, 0.3f, 0.35f, 1.0f}});

        for (int i = 0; i < 8; ++i) {
            const double sub_frame = kBaseFrame + kSubSteps[i];
            const double fraction = kSubSteps[i];
            const float x = x_start + static_cast<float>(fraction * total_dx);

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
        auto fb = renderer.render(animated_scene, frame_int);
        if (!fb) {
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

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

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

        s.rect("bg", {.size = {960, 540}, .color = {0.05f, 0.05f, 0.08f, 1.0f},
                       .pos = {0, 0, 0}});

        constexpr int vw = 320;
        constexpr int vh = 540;
        constexpr float wmin = -10.0f;
        constexpr float wmax = 110.0f;

        for (int view = 0; view < 3; ++view) {
            const int vx0 = view * vw;
            const int vy0 = 0;
            const float scale_x = static_cast<float>(vw) / (wmax - wmin);
            const float scale_y = static_cast<float>(vh) / (wmax - wmin);

            s.line("border_" + std::to_string(view),
                   {.from = {static_cast<float>(vx0), 0, 0},
                    .to = {static_cast<float>(vx0), static_cast<float>(vh), 0},
                    .thickness = 1.0f, .color = {0.2f, 0.2f, 0.3f, 0.5f}});

            float ax1, ay1, ax2, ay2;
            Vec3 p0_2d, p1_2d, p2_2d, p3_2d;
            if (view == 0) {
                ax1 = curve.p0.x; ay1 = curve.p0.y;
                ax2 = curve.p3.x; ay2 = curve.p3.y;
                p0_2d = {curve.p0.x, curve.p0.y, 0};
                p1_2d = {curve.p1.x, curve.p1.y, 0};
                p2_2d = {curve.p2.x, curve.p2.y, 0};
                p3_2d = {curve.p3.x, curve.p3.y, 0};
            } else if (view == 1) {
                ax1 = curve.p0.x; ay1 = curve.p0.z;
                ax2 = curve.p3.x; ay2 = curve.p3.z;
                p0_2d = {curve.p0.x, curve.p0.z, 0};
                p1_2d = {curve.p1.x, curve.p1.z, 0};
                p2_2d = {curve.p2.x, curve.p2.z, 0};
                p3_2d = {curve.p3.x, curve.p3.z, 0};
            } else {
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

            s.layer("label_top", [&](LayerBuilder& l) {
                l.position({50, y_center - 18, 0});
                l.rect("label", {.size = {130, 14}, .color = {0, 0, 0, 0.6f},
                                 .pos = {0, 0, 0}});
            });
        }

        {
            constexpr float y_center = 400.0f;
            constexpr float x_from = 50.0f;
            constexpr float x_to = 910.0f;
            constexpr float x_span = x_to - x_from;

            s.line("track_bot", {.from = {x_from, y_center, 0},
                                 .to = {x_to, y_center, 0},
                                 .thickness = 1.0f, .color = {0.3f, 0.3f, 0.35f, 1.0f}});

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

        s.line("sep_h", {.from = {0, 270, 0}, .to = {960, 270, 0},
                         .thickness = 1.0f, .color = {0.3f, 0.3f, 0.4f, 0.5f}});
        s.line("sep_v", {.from = {480, 0, 0}, .to = {480, 540, 0},
                         .thickness = 1.0f, .color = {0.3f, 0.3f, 0.4f, 0.5f}});

        CubicBezier3D path{
            {0, 0, 0}, {180, 40, 0}, {40, 180, 0}, {200, 200, 0}
        };

        CubicBezier3D linear_path{
            {0, 0, 0}, {66.67f, 0, 0}, {133.33f, 200, 0}, {200, 200, 0}
        };

        constexpr int kMarkers = 16;

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

        {
            constexpr float ox = 500.0f, oy = 10.0f;
            constexpr float sx = 2.0f, sy = 1.2f;
            for (int i = 0; i <= kMarkers; ++i) {
                double raw_t = static_cast<double>(i) / kMarkers;
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

} // namespace chronon3d::test
