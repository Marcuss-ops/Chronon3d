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
}

} // namespace chronon3d::content::two_point_five_d
