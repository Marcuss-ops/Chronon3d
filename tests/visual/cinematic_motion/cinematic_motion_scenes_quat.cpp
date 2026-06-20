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

        constexpr int kCards = 9;
        constexpr float kStartYaw = 179.0f;
        constexpr float kEndYaw = -179.0f;

        for (int i = 0; i < kCards; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kCards - 1);
            Quat q0 = glm::angleAxis(glm::radians(kStartYaw), Vec3{0, 1, 0});
            Quat q1 = glm::angleAxis(glm::radians(kEndYaw),   Vec3{0, 1, 0});
            if (glm::dot(q0, q1) < 0.0f) q1 = -q1;
            Quat q = glm::slerp(q0, q1, t);

            Vec3 fwd = q * Vec3{0, 0, -1};
            Vec3 up  = q * Vec3{0, 1, 0};

            const float cx = 80.0f + static_cast<float>(i) * 90.0f;
            const float cy = 270.0f;

            s.layer("card_" + std::to_string(i), [=](LayerBuilder& l) {
                l.position({cx, cy, 0});
                l.rect("body", {.size = {20, 30}, .color = {0.2f, 0.3f, 0.6f, 1},
                                 .pos = {-10, -15, 0}});
            });

            float arrow_dx = fwd.x * 20.0f;
            float arrow_dy = -fwd.z * 20.0f;
            s.layer("arrow_" + std::to_string(i), [=](LayerBuilder& l) {
                l.line("arr", {.from = {cx, cy, 0},
                                .to = {cx + arrow_dx, cy + arrow_dy, 0},
                                .thickness = 2.0f, .color = {1, 0.8f, 0.2f, 1}});
            });

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

        constexpr int kFrustums = 16;
        for (int i = 0; i < kFrustums; ++i) {
            const double u = static_cast<double>(i) / static_cast<double>(kFrustums - 1);
            const Vec3 pos = path.position(u);
            const Vec3 tangent = path.tangent_at(u);

            Vec3 up{0, -1, 0};
            Vec3 fwd = tangent;
            if (glm::length(fwd) < 1e-6f) fwd = Vec3{1, 0, 0};
            fwd = glm::normalize(fwd);
            Vec3 right = glm::normalize(glm::cross(fwd, up));
            Vec3 actual_up = glm::normalize(glm::cross(right, fwd));

            const float cx = pos.x;
            const float cy = pos.y;
            const float sc = 25.0f;

            s.line("f_" + std::to_string(i),
                   {.from = {cx, cy, 0},
                    .to = {cx + fwd.x * sc, cy - fwd.y * sc, 0},
                    .thickness = 2.0f, .color = {1, 0.8f, 0.2f, 1}});
            s.line("u_" + std::to_string(i),
                   {.from = {cx, cy, 0},
                    .to = {cx + actual_up.x * sc * 0.6f, cy - actual_up.y * sc * 0.6f, 0},
                    .thickness = 1.0f, .color = {0.3f, 1, 0.3f, 1}});
            s.line("r_" + std::to_string(i),
                   {.from = {cx, cy, 0},
                    .to = {cx + right.x * sc * 0.6f, cy - right.y * sc * 0.6f, 0},
                    .thickness = 1.0f, .color = {1, 0.3f, 0.3f, 1}});

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
    auto scene_comp = composition({
        .name = "TwoNodeTargetLockScene",
        .width = 320,
        .height = 180,
        .duration = 61
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(320, 180);

        s.rect("bg", {.size = {320, 180}, .color = {0.05f, 0.05f, 0.08f, 1},
                       .pos = {0, 0, 0}});

        s.null_layer("camera_target", [=](NullBuilder& b) {
            b.position({160, 90, 0});
        });

        s.layer("target_marker", [=](LayerBuilder& l) {
            l.position({160, 90, 0});
            l.rect("tm", {.size = {16, 16}, .color = {0.2f, 0.9f, 0.3f, 1},
                          .pos = {-8, -8, 0}});
        });

        s.camera_rig("main_rig", [=](CameraRigBuilder& rig) {
            rig.two_node("camera_target");
            rig.orbit_yaw(0, 0.0f, 60, 60.0f, EasingCurve{Easing::InOutCubic});
            rig.dolly(0, -200.0f, 60, -100.0f, EasingCurve{Easing::InOutCubic});
            rig.fov(50.0f);
        });

        return s.build();
    });

    constexpr Frame kFrames[5] = {{0}, {15}, {30}, {45}, {60}};
    std::vector<std::shared_ptr<Framebuffer>> panels;
    for (Frame f : kFrames) {
        panels.push_back(renderer.render_frame(scene_comp, f));
    }

    Framebuffer result(960, 540);
    for (int y = 0; y < 540; ++y)
        for (int x = 0; x < 960; ++x)
            result.set_pixel(x, y, {0, 0, 0, 1});

    for (int i = 0; i < 5 && i < static_cast<int>(panels.size()); ++i) {
        if (!panels[i]) continue;
        const int dst_x = i * 192;
        const int dst_y = 180;
        blit_into(result, *panels[i], dst_x, dst_y);

        draw_crosshair(result, dst_x + 160, dst_y + 90, {1, 1, 1, 0.5f}, 8);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 10: MotionBlurShutter
// ═══════════════════════════════════════════════════════════════════════════

Framebuffer render_motion_blur_comparison(SoftwareRenderer& renderer) {
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

    {
        auto s = base;
        s.motion_blur.enabled = false;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

    {
        auto s = base;
        s.motion_blur.enabled = true;
        s.motion_blur.samples = 8;
        s.motion_blur.shutter_angle_deg = 180.0f;
        s.motion_blur.jitter_seed = 0xABCD1234;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

    {
        auto s = base;
        s.motion_blur.enabled = true;
        s.motion_blur.samples = 16;
        s.motion_blur.shutter_angle_deg = 180.0f;
        s.motion_blur.jitter_seed = 0xABCD1234;
        renderer.set_settings(s);
        panels.push_back(renderer.render_frame(anim_comp, kFrame));
    }

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

        const CubicBezier3D curve{
            {50, 400, 0},
            {250, 200, 0},
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
                    .thickness = 2.0f, .color = {0.3f, 0.6f, 1, 0.8f}});
            prev = curr;
        }

        s.line("cp0", {.from = {curve.p0.x, curve.p0.y, 0},
                        .to = {curve.p1.x, curve.p1.y, 0},
                        .thickness = 1.0f, .color = {0.5f, 0.5f, 0.5f, 0.5f}});
        s.line("cp1", {.from = {curve.p2.x, curve.p2.y, 0},
                        .to = {curve.p3.x, curve.p3.y, 0},
                        .thickness = 1.0f, .color = {0.5f, 0.5f, 0.5f, 0.5f}});

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

        const CubicBezier3D curve{
            {50, 400, 0},
            {200, 120, 0},
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
