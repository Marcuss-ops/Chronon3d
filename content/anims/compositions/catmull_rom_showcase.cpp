// content/anims/compositions/catmull_rom_showcase.cpp
// CatmullRomShowcase — camera flying through 4 waypoints using the
// interpolating Catmull-Rom spline.
//
// Render:
//   chronon3d_cli render CatmullRomShowcase --frame 30 -o output/catmull.png

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include "cinematic_showcase_helpers.hpp"

#include <string>

namespace chronon3d::content::anims {

Composition catmull_rom_showcase() {
    return composition({
        .name     = "CatmullRomShowcase",
        .width    = 1280,
        .height   = 720,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_bg(s);

        // Asymmetric background grid — gives the camera movement a visible
        // parallax. Without this, the dark background hides the animation.
        s.layer("grid_far", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 500.0f}).opacity(0.7f);
            l.grid_background("g1", {
                .size = {3000.0f, 1800.0f},
                .grid_color = {0.30f, 0.45f, 0.80f, 1.0f},
                .spacing = 100.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true,
            });
        });

        // Floor grid that sweeps beneath the camera — strongest parallax cue.
        s.layer("floor", [](LayerBuilder& l) {
            l.position({0.0f, 350.0f, 0.0f}).enable_3d().opacity(0.8f);
            l.grid_background("floor_g", {
                .size = {4000.0f, 4000.0f},
                .grid_color = {0.90f, 0.50f, 0.20f, 1.0f},
                .spacing = 120.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 3.0f,
                .major_every = 2,
                .centered = true,
            });
        });

        // Four waypoints in 3D that the camera will visit *exactly*.
        const Vec3 waypoints[] = {
            {-400.0f,  100.0f, -1100.0f},
            {-150.0f, -50.0f,  -900.0f},
            { 150.0f,  80.0f,  -900.0f},
            { 400.0f, -30.0f, -1100.0f},
        };

        // Show the waypoint markers in the world (slightly in front of the
        // camera path so they are visible at every frame).
        waypoint_markers(s, {waypoints[0], waypoints[1], waypoints[2], waypoints[3]});

        // Build the Catmull-Rom camera path.
        CatmullRomCameraMotion motion;
        motion.path.set_alpha(CatmullRomAlpha::Centripetal)
                   .set_boundary(CatmullRomBoundary::Clamped);
        for (Vec3 wp : waypoints) {
            motion.path.add_waypoint(wp);
        }
        motion.auto_orient = AutoOrientMode::TowardsPOI;
        motion.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
        motion.easing = EasingCurve{Easing::InOutCubic};
        motion.zoom = 900.0f;
        motion.fov_deg = 50.0f;

        // Evaluate the camera at the current frame and apply.
        Camera2_5D cam = motion.evaluate(ctx.frame, Frame{0}, Frame{89});
        s.camera().set(cam);

        // A center subject so we can see the camera moving around it.
        s.layer("subject", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f}).glow(GlowParams{.radius = 40.0f, .intensity = 0.9f, .color = {0.4f, 0.8f, 1.0f, 1.0f}});
            l.circle("dot", { .radius = 28.0f, .color = {0.4f, 0.8f, 1.0f, 1.0f} });
        });

        // Frame number label, so the render is clearly time-varying.
        s.layer("hud", [ctx](LayerBuilder& l) {
            l.position({-560.0f, 320.0f, 0.0f});
            l.text("frame_label", TextSpec{
                .content = {.value = "Catmull-Rom: t = " + std::to_string(static_cast<int>(ctx.frame))},
                .font = {.font_size = 18.0f},
                .layout = {.box = {1100.0f, 40.0f}, .align = TextAlign::Left},
                .appearance = {.color = {0.75f, 0.78f, 0.95f, 1.0f}},
                .position = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
