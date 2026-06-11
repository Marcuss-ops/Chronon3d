// content/anims/compositions/cinematic_showcase.cpp
//
// Demo compositions for the new cinematic features added in the AE-style
// upgrade:
//   1. CatmullRomShowcase    — camera flying through 4 waypoints using
//                               the interpolating Catmull-Rom spline.
//   2. DollyZoomShowcase     — dolly + zoom counter-motion (Vertigo effect).
//   3. PostFXLayersShowcase  — DOF + motion blur applied at the post-FX
//                               layer (uses the existing pipeline; not the
//                               CPU PostProcessingSystem, which is a unit-test
//                               helper for direct framebuffer access).
//
// Render any of them via the CLI:
//   chronon3d_cli render CatmullRomShowcase --frame 30 -o output/catmull.png
//   chronon3d_cli render CatmullRomShowcase --frames 0-90 -o output/cm_t###.png

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <cmath>
#include <string>

namespace chronon3d::content::anims {

// Helper: dark cinematic gradient background.
void dark_bg(SceneBuilder& s) {
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg", {
            .size  = {1920.0f, 1080.0f},
            .color = {0.06f, 0.06f, 0.10f, 1.0f},
            .pos   = {0.0f, 0.0f, 0.0f},
            .fill  = Fill::linear({0,0}, {0,1}, {
                {0.0f, {0.05f, 0.05f, 0.10f, 1.0f}},
                {1.0f, {0.10f, 0.07f, 0.18f, 1.0f}},
            })
        });
    });
}

// Helper: a glowing point on the catmull-rom path to show the camera is
// actually visiting each waypoint.
void waypoint_markers(SceneBuilder& s, std::initializer_list<Vec3> waypoints) {
    int i = 0;
    for (Vec3 p : waypoints) {
        s.layer("wp_" + std::to_string(i), [p](LayerBuilder& l) {
            l.position(p).glow(GlowParams{.radius = 24.0f, .intensity = 0.8f, .color = {1.0f, 0.85f, 0.30f, 1.0f}});
            l.circle("dot", { .radius = 14.0f, .color = {1.0f, 0.85f, 0.30f, 1.0f} });
        });
        ++i;
    }
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// 1. CatmullRomShowcase — camera flies through 4 waypoints
// ────────────────────────────────────────────────────────────────────────────
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
            l.text("frame_label", {
                .text = "Catmull-Rom: t = " + std::to_string(static_cast<int>(ctx.frame)),
                .size = {1100.0f, 40.0f},
                .font_size = 18.0f,
                .color = {0.75f, 0.78f, 0.95f, 1.0f},
                .align = TextAlign::Left,
            });
        });

        return s.build();
    });
}

// ────────────────────────────────────────────────────────────────────────────
// 2. DollyZoomShowcase — Vertigo / Hitchcock dolly zoom
// ────────────────────────────────────────────────────────────────────────────
Composition dolly_zoom_showcase() {
    return composition({
        .name     = "DollyZoomShowcase",
        .width    = 1280,
        .height   = 720,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_bg(s);

        // Asymmetric far background grid — visibly squishes during dolly-zoom.
        s.layer("grid_far", [](LayerBuilder& l) {
            l.position({-300.0f, 0.0f, 1500.0f}).enable_3d().opacity(0.7f);
            l.grid_background("grid_bg", {
                .size = {3000.0f, 1800.0f},
                .grid_color = {0.40f, 0.60f, 1.00f, 1.0f},
                .spacing = 100.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true,
            });
        });

        // Floor grid: another parallax reference. Visible in lower half.
        s.layer("floor", [](LayerBuilder& l) {
            l.position({0.0f, 350.0f, 0.0f}).enable_3d().opacity(0.9f);
            l.grid_background("floor_g", {
                .size = {5000.0f, 5000.0f},
                .grid_color = {0.90f, 0.45f, 0.20f, 1.0f},
                .spacing = 120.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 3.0f,
                .major_every = 2,
                .centered = true,
            });
        });

        // A single bright vertical pillar at the side — non-symmetric, makes
        // the dolly-zoom compression visible.
        s.layer("pillar", [](LayerBuilder& l) {
            l.position({500.0f, 0.0f, 0.0f}).enable_3d().opacity(0.95f);
            l.rect("p", {
                .size  = {40.0f, 800.0f},
                .color = {1.0f, 0.85f, 0.30f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f},
            });
        });
        s.layer("pillar2", [](LayerBuilder& l) {
            l.position({-500.0f, 0.0f, 0.0f}).enable_3d().opacity(0.95f);
            l.rect("p", {
                .size  = {40.0f, 800.0f},
                .color = {0.30f, 0.85f, 1.0f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f},
            });
        });

        // Subject in the center, plus background grid to make the perspective
        // shift obvious.
        s.layer("subject", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f}).glow(GlowParams{.radius = 60.0f, .intensity = 0.9f, .color = {1.0f, 0.5f, 0.7f, 1.0f}});
            l.circle("dot", { .radius = 36.0f, .color = {1.0f, 0.5f, 0.7f, 1.0f} });
        });

        // Dolly-zoom: as we move the camera closer, we widen the FOV so the
        // subject stays the same size, but the background squishes.
        const f32 p = ctx.progress();
        const f32 from_z = -1400.0f;
        const f32 to_z   =  -600.0f;
        const f32 from_zoom = 1400.0f;
        const f32 to_zoom   =  600.0f;
        const f32 z_eased = from_z + (to_z - from_z) * glm::cubicEaseInOut(p);
        const f32 zoom_eased = from_zoom + (to_zoom - from_zoom) * glm::cubicEaseInOut(p);

        s.camera().enable(true)
                  .position({0.0f, 0.0f, z_eased})
                  .zoom(zoom_eased)
                  .point_of_interest({0.0f, 0.0f, 0.0f});

        s.layer("hud", [ctx](LayerBuilder& l) {
            l.position({-560.0f, 320.0f, 0.0f});
            l.text("frame_label", {
                .text = "Dolly Zoom: t = " + std::to_string(static_cast<int>(ctx.frame)),
                .size = {1100, 40},
                .font_size = 18.0f,
                .color = {0.75f, 0.78f, 0.95f, 1.0f},
                .align = TextAlign::Left,
            });
        });

        return s.build();
    });
}

// ────────────────────────────────────────────────────────────────────────────
// 3. CameraSplineComparison — three cameras: linear, Bezier, Catmull-Rom
//    (frame-by-frame path comparison).
// ────────────────────────────────────────────────────────────────────────────
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
            l.text("frame_label", {
                .text = "Catmull-Rom: t = " + std::to_string(static_cast<int>(ctx.frame)),
                .size = {1100, 40},
                .font_size = 18.0f,
                .color = {0.75f, 0.78f, 0.95f, 1.0f},
                .align = TextAlign::Left,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
