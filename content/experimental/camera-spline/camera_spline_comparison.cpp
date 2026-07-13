// content/anims/compositions/camera_spline_comparison.cpp
// CameraSplineComparison — camera path through a 5×5 grid of spheres
// using Catmull-Rom spline interpolation.
//
// Render:
//   chronon3d_cli render CameraSplineComparison --frame 30 -o output/spline.png

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/path/catmull_rom_path.hpp>

#include "content/showcases/cinematic/cinematic_showcase_helpers.hpp"

#include <string>
#include <chronon3d/text/text_definition.hpp>

namespace chronon3d::content::anims {

Composition camera_spline_comparison() {
    return composition({
        .name     = "CameraSplineComparison",
        .width    = 1280,
        .height   = 720,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_bg(s);

        // Floor grid: strong parallax cue, makes camera movement obvious.
        s.layer("floor", [](LayerBuilder& l) {
            l.position({0.0f, 300.0f, 0.0f}).enable_3d().opacity(0.85f);
            l.grid_background("floor_g", {
                .size = {5000.0f, 5000.0f},
                .grid_color = {0.85f, 0.55f, 0.25f, 1.0f},
                .spacing = 150.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 3.0f,
                .major_every = 2,
                .centered = true,
            });
        });

        // A grid of 5×5 spheres on the Z=0 plane — the camera will pass by.
        for (i32 gx = -2; gx <= 2; ++gx) {
            for (i32 gy = -2; gy <= 2; ++gy) {
                s.layer("g_" + std::to_string(gx) + "_" + std::to_string(gy),
                        [gx, gy](LayerBuilder& l) {
                    l.position({gx * 120.0f, gy * 120.0f, 0.0f})
                     .glow(GlowParams{.radius = 18.0f, .intensity = 0.7f, .color = {0.5f, 0.85f, 1.0f, 1.0f}});
                    l.circle("dot", { .radius = 16.0f, .color = {0.5f, 0.85f, 1.0f, 1.0f} });
                });
            }
        }

        // The camera uses a Catmull-Rom path through 4 corners of the scene.
        CatmullRomCameraMotion motion;
        motion.path.set_alpha(CatmullRomAlpha::Centripetal)
                   .set_boundary(CatmullRomBoundary::Clamped);
        motion.path.add_waypoint({-300.0f, -200.0f, -1100.0f})
                   .add_waypoint({ 300.0f, -100.0f,  -900.0f})
                   .add_waypoint({ 300.0f,  200.0f, -1100.0f})
                   .add_waypoint({-300.0f,  100.0f,  -900.0f});
        motion.auto_orient = AutoOrientMode::TowardsPOI;
        motion.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
        motion.zoom = 800.0f;
        motion.fov_deg = 60.0f;

        Camera2_5D cam = motion.evaluate(ctx.frame, Frame{0}, Frame{119});
        s.camera().set(cam);

        s.layer("hud", [ctx](LayerBuilder& l) {
            l.position({-560.0f, 320.0f, 0.0f});
            l.text("frame_label", TextDefinition{
    .content = {.value = "Catmull-Rom: t = " + std::to_string(static_cast<int>(ctx.frame))},
    .style = {
        .font = {.font_size = 18.0f},
        .color = {0.75f, 0.78f, 0.95f, 1.0f}
    },
    .frame = {
        .size = {1100, 40},
        .align = TextAlign::Left
    }
});
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
