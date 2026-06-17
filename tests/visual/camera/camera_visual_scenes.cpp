#include <chronon3d/scene/builders/scene_builder.hpp>
#include "camera_visual_scenes.hpp"

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
using namespace chronon3d;

namespace chronon3d::test {

Composition make_center_target_composition() {
    return composition(
        {.name = "CameraVisualCenterTarget", .width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.063f, 0.063f, 0.094f, 1.0f},
                    .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            s.null_layer("target", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
            });

            s.layer("center_cross", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("h", {.size = {80.0f, 10.0f}, .color = Color::white(), .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color::white())});
                l.rect("v", {.size = {10.0f, 80.0f}, .color = Color::white(), .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color::white())});
            });

            s.layer("left_marker", [](LayerBuilder& l) {
                l.enable_3d().position({-300.0f, 0.0f, 0.0f});
                l.rect("m", {.size = {80.0f, 80.0f}, .color = Color{0.0f, 0.5f, 1.0f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.0f, 0.5f, 1.0f, 1.0f})});
            });

            s.layer("right_marker", [](LayerBuilder& l) {
                l.enable_3d().position({300.0f, 0.0f, 0.0f});
                l.rect("m", {.size = {80.0f, 80.0f}, .color = Color{1.0f, 0.2f, 0.2f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{1.0f, 0.2f, 0.2f, 1.0f})});
            });

            s.camera().enable()
                .position({0.0f, 0.0f, -1000.0f})
                .target("target")
                .zoom(1000.0f);

            return s.build();
        }
    );
}

Composition make_parallax_stack_composition() {
    return composition(
        {.name = "CameraVisualParallaxStack", .width = 960, .height = 540, .duration = 31},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                    .grid_color = {0.18f, 0.75f, 1.0f, 0.09f},
                    .spacing = 50.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            s.layer("far_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 500.0f});
                l.rect("card", {.size = {120.0f, 120.0f}, .color = Color{0.2f, 0.4f, 1.0f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.2f, 0.4f, 1.0f, 1.0f})});
            });

            s.layer("mid_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {120.0f, 120.0f}, .color = Color{0.2f, 1.0f, 0.4f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.2f, 1.0f, 0.4f, 1.0f})});
            });

            s.layer("near_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, -300.0f});
                l.rect("card", {.size = {120.0f, 120.0f}, .color = Color::red(), .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color::red())});
            });

            AnimatedCamera2_5D cam;
            cam.position
                .key(0, Vec3{0.0f, 0.0f, -1000.0f})
                .key(30, Vec3{200.0f, 0.0f, -1000.0f}, EasingCurve{Easing::InOutCubic});
            cam.zoom.set(1000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            return s.build();
        }
    );
}

Composition make_orbit_two_node_composition() {
    return composition(
        {.name = "CameraVisualOrbitTwoNode", .width = 960, .height = 540, .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.015f, 0.015f, 0.035f, 1.0f},
                    .grid_color = {0.18f, 0.75f, 1.0f, 0.09f},
                    .spacing = 50.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            s.layer("subject", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("card", {.size = {280.0f, 140.0f}, .radius = 22.0f,
                    .color = Color{0.99f, 0.44f, 0.82f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.99f, 0.44f, 0.82f, 1.0f})});
            });

            s.layer("accent", [](LayerBuilder& l) {
                l.enable_3d().position({240.0f, -20.0f, -180.0f});
                l.circle("dot", {.radius = 24.0f, .color = Color{0.15f, 0.85f, 1.0f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.15f, 0.85f, 1.0f, 1.0f})});
            });

            AnimatedCamera2_5D cam;
            const f32 r = 1200.0f;
            const f32 start_angle = glm::radians(-25.0f);
            const f32 end_angle = glm::radians(25.0f);
            constexpr int kSamples = 5;
            for (int i = 0; i <= kSamples; ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(kSamples);
                const f32 angle = start_angle + (end_angle - start_angle) * t;
                const Frame f = Frame{static_cast<i32>(std::round(t * 60.0f))};
                cam.position.key(f, Vec3{
                    r * std::sin(angle),
                    0.0f,
                    -r * std::cos(angle)
                });
            }
            cam.zoom.set(1000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            return s.build();
        }
    );
}

Composition make_near_plane_crossing_composition() {
    return composition(
        {.name = "CameraVisualNearPlaneCrossing", .width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                    .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            s.layer("cutting_card", [](LayerBuilder& l) {
                l.enable_3d()
                 .position({0.0f, 0.0f, -999.5f})
                 .rotate({0.0f, 82.0f, 0.0f});
                l.rect("card", {.size = {600.0f, 400.0f}, .color = Color{0.9f, 0.9f, 0.9f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.9f, 0.9f, 0.9f, 1.0f})});
            });

            s.camera().enable()
                .position({0.0f, 0.0f, -1000.0f})
                .point_of_interest({0.0f, 0.0f, 0.0f})
                .zoom(1000.0f);

            return s.build();
        }
    );
}

Composition make_z_sort_stack_composition() {
    return composition(
        {.name = "CameraVisualZSortStack", .width = 960, .height = 540, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                    .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            s.layer("far_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 500.0f});
                l.rect("card", {.size = {200.0f, 200.0f}, .color = Color{0.2f, 0.4f, 1.0f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.2f, 0.4f, 1.0f, 1.0f})});
            });

            s.layer("mid_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {200.0f, 200.0f}, .color = Color{0.2f, 1.0f, 0.4f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{0.2f, 1.0f, 0.4f, 1.0f})});
            });

            s.layer("near_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, -300.0f});
                l.rect("card", {.size = {200.0f, 200.0f}, .color = Color{1.0f, 0.2f, 0.2f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::solid(Color{1.0f, 0.2f, 0.2f, 1.0f})});
            });

            s.camera().enable()
                .position({0.0f, 0.0f, -1000.0f})
                .point_of_interest({0.0f, 0.0f, 0.0f})
                .zoom(1000.0f);

            return s.build();
        }
    );
}

} // namespace chronon3d::test
