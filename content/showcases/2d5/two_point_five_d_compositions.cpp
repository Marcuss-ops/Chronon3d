#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cmath>
#include <string>

namespace chronon3d::content::two_point_five_d {
namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

void add_depth_card(SceneBuilder& scene,
                    const std::string& id,
                    Vec3 position,
                    Vec2 size,
                    Color color,
                    Vec3 rotation = {0.0f, 0.0f, 0.0f}) {
    scene.layer("card_" + id, [=](LayerBuilder& layer) {
        layer.enable_3d().position(position).rotate(rotation);
        layer.rounded_rect("surface", {
            .size = size,
            .radius = 24.0f,
            .color = color,
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });
}

Composition parallax_simple() {
    return composition({
        .name = "ParallaxSimple",
        .width = kWidth,
        .height = kHeight,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        const f32 progress = ctx.progress();
        const f32 camera_x = -progress * 360.0f;
        scene.camera().set(camera_motion::parallax_sweep(progress, 90.0f, -1000.0f, 820.0f));

        scene.layer("far_background", [camera_x](LayerBuilder& layer) {
            layer.enable_3d().position({camera_x * 0.2f, 0.0f, 300.0f});
            layer.rect("background", {
                .size = {kWidth * 2.0f, kHeight},
                .color = {0.02f, 0.04f, 0.12f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        scene.layer("mid_background", [camera_x](LayerBuilder& layer) {
            layer.enable_3d().position({camera_x * 0.5f, 0.0f, 120.0f});
            layer.rect("background", {
                .size = {kWidth * 1.5f, kHeight},
                .color = {0.05f, 0.08f, 0.20f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        add_depth_card(scene, "left", {-360.0f + camera_x * 0.5f, 0.0f, 80.0f},
                       {260.0f, 420.0f}, {0.12f, 0.30f, 0.68f, 0.92f}, {0.0f, -8.0f, 0.0f});
        add_depth_card(scene, "center", {0.0f + camera_x, 0.0f, -40.0f},
                       {340.0f, 500.0f}, {0.22f, 0.16f, 0.60f, 0.96f});
        add_depth_card(scene, "right", {360.0f + camera_x, 0.0f, -140.0f},
                       {260.0f, 420.0f}, {0.08f, 0.44f, 0.62f, 0.92f}, {0.0f, 8.0f, 0.0f});
        return scene.build();
    });
}

Composition depth_scene() {
    return composition({
        .name = "DepthScene",
        .width = kWidth,
        .height = kHeight,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        scene.camera().set(camera_motion::push_in_tilt(ctx.progress(), {
            .from_z = -900.0f,
            .to_z = -620.0f,
            .from_tilt = -6.0f,
            .to_tilt = 5.0f,
            .zoom = 860.0f
        }));
        scene.ambient_light({1.0f, 1.0f, 1.0f, 1.0f}, 0.18f);
        scene.directional_light({-0.35f, 1.0f, -0.55f}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.82f);
        scene.layer("backdrop", [](LayerBuilder& layer) {
            layer.enable_3d().position({0.0f, 0.0f, 300.0f});
            layer.rect("backdrop", {
                .size = {1800.0f, 1000.0f},
                .color = {0.03f, 0.06f, 0.16f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        add_depth_card(scene, "far", {-260.0f, 0.0f, 180.0f},
                       {360.0f, 300.0f}, {0.10f, 0.20f, 0.42f, 0.92f});
        add_depth_card(scene, "mid", {0.0f, -20.0f, 20.0f},
                       {520.0f, 400.0f}, {0.18f, 0.24f, 0.52f, 0.96f});
        add_depth_card(scene, "near", {260.0f, 40.0f, -180.0f},
                       {300.0f, 240.0f}, {0.24f, 0.54f, 0.88f, 0.96f}, {0.0f, 10.0f, 0.0f});
        return scene.build();
    });
}

Composition card_flip() {
    return composition({
        .name = "CardFlip",
        .width = kWidth,
        .height = kHeight,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        const f32 angle = ctx.progress() * 360.0f;
        const f32 facing = std::cos(angle * 0.0174532925f);
        scene.camera().set(camera_motion::orbit_small(ctx.progress(), 820.0f));
        scene.layer("background", [](LayerBuilder& layer) {
            layer.rect("background", {
                .size = {kWidth, kHeight},
                .color = {0.008f, 0.012f, 0.028f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        scene.layer("card", [angle, facing](LayerBuilder& layer) {
            layer.enable_3d().rotate({0.0f, angle, 0.0f});
            const Color front = {0.14f, 0.22f, 0.52f, 1.0f};
            const Color back = {0.10f, 0.12f, 0.28f, 1.0f};
            layer.rounded_rect("face", {
                .size = {420.0f, 580.0f},
                .radius = 28.0f,
                .color = facing >= 0.0f ? front : back,
                .pos = {0.0f, 0.0f, 0.0f}
            });
            layer.rect("accent", {
                .size = {340.0f, 18.0f},
                .color = facing >= 0.0f ? Color{0.28f, 0.72f, 1.0f, 0.85f}
                                        : Color{0.42f, 0.34f, 0.86f, 0.85f},
                .pos = {0.0f, -190.0f, 0.1f}
            });
        });
        return scene.build();
    });
}

Composition dof_showcase() {
    return composition({
        .name = "DofShowcase",
        .width = kWidth,
        .height = kHeight,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        camera_v1::PoseTracksSource camera;
        camera.position.set({0.0f, 0.0f, -1000.0f});
        camera.zoom.set(1000.0f);
        camera.target.set({0.0f, 0.0f, 0.0f});
        camera.use_target = true;
        camera.aperture.set(0.025f);
        camera.max_blur.set(24.0f);
        camera.focus_distance
            .key(Frame{0}, -360.0f)
            .key(Frame{60}, 0.0f)
            .key(Frame{119}, 360.0f);
        scene.camera_pose(camera);
        scene.layer("background", [](LayerBuilder& layer) {
            layer.rect("background", {
                .size = {kWidth, kHeight},
                .color = {0.015f, 0.025f, 0.07f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        add_depth_card(scene, "background_bokeh", {-420.0f, -40.0f, 360.0f},
                       {280.0f, 280.0f}, {0.16f, 0.30f, 0.72f, 0.42f});
        add_depth_card(scene, "focus_subject", {0.0f, 0.0f, 0.0f},
                       {460.0f, 500.0f}, {0.18f, 0.52f, 0.78f, 0.98f});
        add_depth_card(scene, "foreground_bokeh", {400.0f, 80.0f, -260.0f},
                       {300.0f, 300.0f}, {0.72f, 0.26f, 0.64f, 0.38f});
        return scene.build();
    });
}

// Temporary certification showcase for the user-facing 2.5D typography
// surface.  It intentionally uses no image or mesh assets.
Composition text_25d_tests(std::size_t first_section = 0,
                           std::string composition_name = "Text25DTests") {
    return composition({
        .name = std::move(composition_name),
        .width = kWidth,
        .height = kHeight,
        .duration = first_section == 0 ? 600 : 120
    }, [first_section](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        const auto frame = ctx.frame().integral();
        const auto section = first_section == 0 ? frame / 120 : first_section;
        scene.camera().enable(true)
            .position({0.0f, 0.0f, -1000.0f})
            .zoom(1000.0f)
            .look_at({0.0f, 0.0f, 0.0f});
        scene.layer("background", [section](LayerBuilder& layer) {
            const Color colors[] = {
                {0.008f, 0.012f, 0.028f, 1.0f},
                {0.025f, 0.012f, 0.045f, 1.0f},
                {0.008f, 0.035f, 0.050f, 1.0f},
                {0.035f, 0.020f, 0.008f, 1.0f},
                {0.012f, 0.025f, 0.045f, 1.0f},
            };
            layer.rect("background", {
                .size = {kWidth, kHeight},
                .color = colors[std::min<std::size_t>(section, 4)],
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        const auto add_text = [&](std::string id, std::string value,
                                  Vec3 position, Vec3 rotation,
                                  Vec3 scale = {1.0f, 1.0f, 1.0f},
                                  f32 opacity = 1.0f) {
            const auto animation_id = id;
            scene.layer(std::move(id), [animation_id, value = std::move(value), position,
                                        rotation, scale, opacity](LayerBuilder& layer) {
                layer.enable_3d().position(position)
                    .rotate(rotation)
                    .scale(scale)
                    .opacity(opacity);
                layer.text("headline", TextDefinition{
                    .content = {.value = value},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size = 190.0f
                    }, .color = Color::white()},
                    .frame = {
                        .size = {1700.0f, 420.0f},
                        .placement = TextPlacement{TextPlacementKind::Absolute,
                                                    {960.0f, 540.0f}},
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle
                    }
                });
                if (animation_id == "t01") {
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, -75.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 0.0f, 0.0f});
                } else if (animation_id == "t02") {
                    layer.rotate_anim().key(Frame{0}, Vec3{75.0f, 0.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 0.0f, 0.0f});
                } else if (animation_id == "t03") {
                    // The TextRun surface is currently rasterized into the
                    // full canvas. Keep the authored left pivot aligned with
                    // that surface's centered origin until tight-surface
                    // allocation lands in the shared text path.
                    layer.position({-1280.0f, 0.0f, 0.0f});
                    layer.anchor({-720.0f, 0.0f, 0.0f});
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, -90.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 0.0f, 0.0f});
                } else if (animation_id == "hero") {
                    layer.rotate_anim().key(Frame{0}, Vec3{15.0f, -25.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 0.0f, 0.0f});
                }
            });
        };

        if (section == 0) {
            add_text("t01", "THE FUTURE", {0.0f, 0.0f, 0.0f},
                     {0.0f, -75.0f, 0.0f});
        } else if (section == 1) {
            add_text("t02", "HISTORY", {0.0f, 0.0f, 0.0f},
                     {75.0f, 0.0f, 0.0f});
        } else if (section == 2) {
            add_text("t03", "REVOLUTION", {0.0f, 0.0f, 0.0f},
                     {0.0f, -90.0f, 0.0f});
        } else if (section == 3) {
            camera_v1::PoseTracksSource camera;
            camera.position
                .key(Frame{0}, Vec3{-260.0f, 0.0f, -1000.0f})
                .key(Frame{119}, Vec3{260.0f, 0.0f, -1000.0f});
            camera.zoom.set(1000.0f);
            camera.target.set({0.0f, 0.0f, 0.0f});
            camera.use_target = true;
            scene.camera_pose(camera);
            add_text("p_far", "THE", {0.0f, -250.0f, 220.0f},
                     {0.0f, 0.0f, 0.0f}, {0.75f, 0.75f, 1.0f});
            add_text("p_mid", "FUTURE", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f});
            add_text("p_near", "IS HERE", {0.0f, 250.0f, -220.0f},
                     {0.0f, 0.0f, 0.0f}, {1.15f, 1.15f, 1.0f});
        } else {
            const f32 progress = ctx.progress();
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1250.0f + progress * 100.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});
            add_text("hero", "THE UNTOLD STORY", {0.0f, 0.0f, 0.0f},
                     {15.0f, -25.0f, 0.0f}, {0.62f, 0.62f, 1.0f});
        }
        return scene.build();
    });
}

} // namespace

void register_two_point_five_d_compositions(CompositionRegistry& registry) {
    registry.add(make_composition_descriptor("ParallaxSimple", [](const CompositionProps&) {
        return parallax_simple();
    }));
    registry.add(make_composition_descriptor("DepthScene", [](const CompositionProps&) {
        return depth_scene();
    }));
    registry.add(make_composition_descriptor("CardFlip", [](const CompositionProps&) {
        return card_flip();
    }));
    registry.add(make_composition_descriptor("DofShowcase", [](const CompositionProps&) {
        return dof_showcase();
    }));
    registry.add(make_composition_descriptor("Text25DTests", [](const CompositionProps&) {
        return text_25d_tests();
    }));
    registry.add(make_composition_descriptor("Text25D-T01-YRotation", [](const CompositionProps&) {
        return text_25d_tests(0, "Text25D-T01-YRotation");
    }));
    registry.add(make_composition_descriptor("Text25D-T02-XFlip", [](const CompositionProps&) {
        return text_25d_tests(1, "Text25D-T02-XFlip");
    }));
    registry.add(make_composition_descriptor("Text25D-T03-Pivot", [](const CompositionProps&) {
        return text_25d_tests(2, "Text25D-T03-Pivot");
    }));
    registry.add(make_composition_descriptor("Text25D-T04-Parallax", [](const CompositionProps&) {
        return text_25d_tests(3, "Text25D-T04-Parallax");
    }));
    registry.add(make_composition_descriptor("Text25D-T05-DocumentaryHero", [](const CompositionProps&) {
        return text_25d_tests(4, "Text25D-T05-DocumentaryHero");
    }));
}

} // namespace chronon3d::content::two_point_five_d
