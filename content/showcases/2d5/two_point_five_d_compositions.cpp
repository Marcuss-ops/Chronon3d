#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cmath>
#include <algorithm>
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
        const Color colors[] = {
            {0.008f, 0.012f, 0.028f, 1.0f},
            {0.025f, 0.012f, 0.045f, 1.0f},
            {0.008f, 0.035f, 0.050f, 1.0f},
            {0.035f, 0.020f, 0.008f, 1.0f},
            {0.012f, 0.025f, 0.045f, 1.0f},
        };
        const auto color = colors[std::min<std::size_t>(section, 4)];
        if (section >= 6) {
            // Camera stress scenes keep the backdrop in framebuffer space so
            // their measurements describe the camera/text path, not a second
            // projected surface.
            scene.screen_layer("background", [color](LayerBuilder& layer) {
                layer.fill(color);
            });
        } else {
            scene.layer("background", [color](LayerBuilder& layer) {
                // The active 2.5D source pass already applies the canvas-center
                // translation; compensate it here so the fill covers the frame.
                layer.position({-kWidth * 0.5f, kHeight * 0.5f, 0.0f});
                layer.fill(color);
            });
        }

        const auto add_text = [&](std::string id, std::string value,
                                  Vec3 position, Vec3 rotation,
                                  Vec3 scale = {1.0f, 1.0f, 1.0f},
                                  f32 opacity = 1.0f,
                                  f32 font_size = 190.0f,
                                  Vec2 frame_size = {1700.0f, 420.0f}) {
            const auto animation_id = id;
            scene.layer(std::move(id), [animation_id, value = std::move(value), position,
                                        rotation, scale, opacity, font_size,
                                        frame_size](LayerBuilder& layer) {
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
                        .font_size = font_size
                    }, .color = Color::white()},
                    .frame = {
                        .size = frame_size,
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
                    // Tight Raster Surface owns the local origin. The authored
                    // layer therefore keeps its semantic pivot at the canvas
                    // origin instead of compensating for a full-canvas raster.
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, -90.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 0.0f, 0.0f});
                } else if (animation_id == "t08") {
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, -89.0f, 0.0f})
                        .key(Frame{60}, Vec3{0.0f, 89.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, -89.0f, 0.0f});
                } else if (animation_id == "t19") {
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f})
                        .key(Frame{119}, Vec3{180.0f, 720.0f, 0.0f});
                } else if (animation_id == "t12_giant") {
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, -80.0f, 0.0f})
                        .key(Frame{30}, Vec3{0.0f, -35.0f, 0.0f})
                        .key(Frame{60}, Vec3{0.0f, 0.0f, 0.0f})
                        .key(Frame{90}, Vec3{0.0f, 35.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 80.0f, 0.0f});
                    layer.scale_anim().key(Frame{0}, Vec3{0.22f, 0.22f, 1.0f})
                        .key(Frame{60}, Vec3{1.0f, 1.0f, 1.0f})
                        .key(Frame{119}, Vec3{0.22f, 0.22f, 1.0f});
                } else if (animation_id == "t13_scale") {
                    // T13 deliberately crosses two orders of magnitude. The
                    // source remains a tight text surface; only the projected
                    // transform and its scratch/output extents should grow.
                    layer.rotate_anim().key(Frame{0}, Vec3{0.0f, -72.0f, 0.0f})
                        .key(Frame{30}, Vec3{0.0f, -24.0f, 0.0f})
                        .key(Frame{60}, Vec3{0.0f, 0.0f, 0.0f})
                        .key(Frame{90}, Vec3{0.0f, 28.0f, 0.0f})
                        .key(Frame{119}, Vec3{0.0f, 72.0f, 0.0f});
                    layer.scale_anim().key(Frame{0}, Vec3{0.02f, 0.02f, 1.0f})
                        .key(Frame{20}, Vec3{0.10f, 0.10f, 1.0f})
                        .key(Frame{60}, Vec3{5.0f, 5.0f, 1.0f})
                        .key(Frame{90}, Vec3{1.0f, 1.0f, 1.0f})
                        .key(Frame{119}, Vec3{0.02f, 0.02f, 1.0f});
                } else if (animation_id == "t22_long") {
                    layer.rotate_anim().key(Frame{0}, rotation)
                        .key(Frame{119}, Vec3{rotation.x, rotation.y + 18.0f,
                                              rotation.z + 6.0f});
                } else if (animation_id.rfind("t23_random_", 0) == 0) {
                    layer.rotate_anim().key(Frame{0}, rotation)
                        .key(Frame{119}, Vec3{rotation.x + 24.0f,
                                              rotation.y + 55.0f,
                                              rotation.z + 18.0f});
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
        } else if (section == 5) {
            add_text("t08", "PERSPECTIVE", {0.0f, 0.0f, 0.0f},
                     {0.0f, -89.0f, 0.0f});
        } else if (section == 6) {
            camera_v1::PoseTracksSource camera;
            camera.position
                .key(Frame{0}, Vec3{120.0f, 0.0f, -1800.0f})
                .key(Frame{30}, Vec3{120.0f, 0.0f, -1000.0f})
                .key(Frame{60}, Vec3{120.0f, 0.0f, -400.0f})
                .key(Frame{90}, Vec3{120.0f, 0.0f, -50.0f})
                .key(Frame{119}, Vec3{120.0f, 0.0f, 300.0f});
            camera.zoom.set(1000.0f);
            camera.target.set({0.0f, 0.0f, 0.0f});
            // The lateral offset keeps look-at well-defined at z=0. The
            // resulting basis transition is intentional: T07 must expose
            // whether camera roll/backface semantics remain continuous.
            camera.use_target = true;
            scene.camera_pose(camera);
            add_text("t07", "THE FUTURE", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f});
        } else if (section == 7) {
            camera_v1::PoseTracksSource camera;
            camera.position
                .key(Frame{0}, Vec3{0.0f, 0.0f, -1800.0f})
                .key(Frame{119}, Vec3{0.0f, 0.0f, 300.0f});
            camera.zoom.set(1000.0f);
            camera.target.set({0.0f, 0.0f, 0.0f});
            camera.use_target = true;
            scene.camera_pose(camera);

            // T14: twenty independent projected surfaces form a depth tunnel.
            // The camera crosses the stack, exercising ordering, near-plane
            // handling, surface lifetime and composite pressure together.
            for (int i = 0; i < 20; ++i) {
                const f32 z = 900.0f - static_cast<f32>(i) * 100.0f;
                const f32 scale = 0.52f + static_cast<f32>(i % 4) * 0.04f;
                add_text("t14_tunnel_" + std::to_string(i), "FUTURE",
                         {0.0f, 0.0f, z}, {0.0f, 0.0f, 0.0f},
                         {scale, scale, 1.0f}, 0.82f);
            }
        } else if (section == 8) {
            const f32 tau = 6.28318530718f;
            const f32 phase = ctx.progress() * tau;
            const f32 orbit = phase * 0.35f;
            scene.camera().enable(true)
                .position({std::sin(orbit) * 220.0f,
                           std::cos(orbit) * 90.0f,
                           -1250.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T15: one independently projected surface per glyph. The
            // helix rotates in world space while the camera performs a
            // shallow orbit, exercising XYZ transforms, depth ordering and
            // compositing without hiding failures behind a single surface.
            constexpr const char* kGlyphs = "CHRONON3D";
            constexpr int kGlyphCount = 9;
            for (int i = 0; i < kGlyphCount; ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(kGlyphCount - 1);
                const f32 angle = phase + t * tau * 1.35f;
                const f32 radius = 300.0f;
                const Vec3 position{
                    std::cos(angle) * radius,
                    std::sin(angle) * radius,
                    (t - 0.5f) * 700.0f};
                const Vec3 rotation{
                    std::sin(phase + t * tau) * 24.0f,
                    std::cos(phase * 0.8f + t * tau) * 48.0f,
                    angle * 57.2957795f + 90.0f};
                const f32 scale = 0.62f + 0.08f * std::sin(t * tau + phase);
                add_text("t15_helix_" + std::to_string(i),
                         std::string(1, kGlyphs[i]), position, rotation,
                         {scale, scale, 1.0f}, 0.94f);
            }
        } else if (section == 9) {
            const f32 tau = 6.28318530718f;
            const f32 phase_time = ctx.progress() * tau;
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1250.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T16: medium-scale executor/compositor stress. Every cell is a
            // real TextRun and therefore owns its projected surface, while
            // the phase offset prevents accidental temporal deduplication.
            constexpr int kColumns = 8;
            constexpr int kRows = 6;
            for (int row = 0; row < kRows; ++row) {
                for (int column = 0; column < kColumns; ++column) {
                    const int index = row * kColumns + column;
                    const f32 cell_phase = phase_time + static_cast<f32>(index) * 0.17f;
                    const Vec3 position{
                        (static_cast<f32>(column) - 3.5f) * 235.0f,
                        (static_cast<f32>(row) - 2.5f) * 175.0f,
                        std::sin(cell_phase) * 300.0f};
                    const Vec3 rotation{
                        std::cos(cell_phase) * 20.0f,
                        std::sin(cell_phase) * 60.0f,
                        std::sin(cell_phase * 0.7f) * 8.0f};
                    const f32 scale = 0.34f + 0.025f * std::cos(cell_phase);
                    add_text("t16_wall_" + std::to_string(index), "AI",
                             position, rotation, {scale, scale, 1.0f}, 0.90f);
                }
            }
        } else if (section == 10) {
            const f32 tau = 6.28318530718f;
            const f32 angle = ctx.progress() * tau;
            const f32 orbit_x = std::sin(angle) * 620.0f;
            const f32 orbit_y = std::cos(angle) * 260.0f;
            scene.camera().enable(true)
                .position({orbit_x, orbit_y, -1100.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T17: the authored text and pivot are static; only the camera
            // orbits the subject. Any drift or discontinuity is therefore a
            // camera/projection defect, not a layer animation artifact.
            add_text("t17_orbit", "CAMERA ORBIT", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f}, {0.78f, 0.78f, 1.0f});
        } else if (section == 11) {
            const f32 progress = ctx.progress();
            const f32 distance = 650.0f + progress * 1250.0f;
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -distance})
                .zoom(distance)
                .look_at({0.0f, 0.0f, 0.0f});

            // T18: compensate the camera dolly with focal zoom. The static
            // subject should retain a stable projected bbox; any large drift
            // indicates a distance/zoom contract regression.
            add_text("t18_dolly_zoom", "DOLLY ZOOM", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f}, {0.82f, 0.82f, 1.0f});
        } else if (section == 12) {
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1100.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T19: high angular velocity with temporal supersampling enabled
            // by the render command. The frame should remain finite and the
            // blur should follow the rotation rather than create ghost quads.
            add_text("t19", "SPEED", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f}, {0.86f, 0.86f, 1.0f});
        } else if (section == 13) {
            const f32 progress = ctx.progress();
            const f32 focus_z = -600.0f + progress * 1200.0f;
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1200.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                    .enabled = true,
                    .focus_z = focus_z,
                    .aperture = 0.035f,
                    .max_blur = 24.0f
                });

            // T20: eight independent text surfaces at different depths. The
            // animated focus plane should move blur through the corridor while
            // preserving deterministic alpha/compositing semantics.
            constexpr const char* kWords[] = {
                "PAST", "HISTORY", "PRESENT", "TODAY",
                "CHANGE", "FUTURE", "TOMORROW", "BEYOND"
            };
            for (int i = 0; i < 8; ++i) {
                const f32 t = static_cast<f32>(i) / 7.0f;
                const Vec3 position{
                    (static_cast<f32>(i % 2) - 0.5f) * 420.0f,
                    (static_cast<f32>(i / 2) - 1.5f) * 180.0f,
                    -600.0f + t * 1200.0f};
                add_text("t20_dof_" + std::to_string(i), kWords[i],
                         position, {0.0f, 0.0f, 0.0f},
                         {0.58f, 0.58f, 1.0f}, 0.94f);
            }
        } else if (section == 14) {
            const f32 progress = ctx.progress();
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1250.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T21: twenty translucent projected surfaces overlap through a
            // depth corridor. The small XY offsets keep every layer
            // independently observable while preserving heavy overdraw.
            constexpr const char* kWords[] = {
                "ALPHA", "DEPTH", "BLEND", "LAYER", "STACK"
            };
            for (int i = 0; i < 20; ++i) {
                const f32 t = static_cast<f32>(i) / 19.0f;
                const f32 phase = progress * 6.28318530718f + t * 6.28318530718f;
                const Vec3 position{
                    std::sin(phase) * 115.0f,
                    std::cos(phase * 1.17f) * 75.0f,
                    -700.0f + t * 1400.0f};
                const Vec3 rotation{
                    std::sin(phase) * 7.0f,
                    std::cos(phase * 0.83f) * 11.0f,
                    std::sin(phase * 0.61f) * 4.0f};
                const f32 opacity = 0.08f + static_cast<f32>(i % 5) * 0.035f;
                const f32 scale = 0.56f + static_cast<f32>(i % 4) * 0.025f;
                add_text("t21_opacity_" + std::to_string(i),
                         kWords[i % 5], position, rotation,
                         {scale, scale, 1.0f}, opacity);
            }
        } else if (section == 15) {
            const f32 progress = ctx.progress();
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1550.0f})
                .zoom(850.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T22: one long paragraph exercises shaping, line breaking,
            // tight-surface allocation and projective sampling together.
            // The deliberate tilt changes the projected bbox without using
            // manual canvas offsets or a full-frame text surface.
            const std::string long_text =
                "CHRONON 3D IS A CPU-FIRST MOTION GRAPHICS RENDERER BUILT "
                "FOR DETERMINISTIC TEXT, CAMERA AND COMPOSITING WORKFLOWS. "
                "THIS LONG SENTENCE INTENTIONALLY CROSSES SHAPING, WRAPPING, "
                "RASTERIZATION, TIGHT SURFACE, TRANSFORM AND PROJECTIVE "
                "SAMPLING BOUNDARIES IN ONE ANIMATED TYPOGRAPHIC SURFACE.";
            add_text("t22_long", long_text, {0.0f, 0.0f, 0.0f},
                     {0.0f, -42.0f + progress * 18.0f, -4.0f},
                     {0.55f, 0.55f, 1.0f}, 1.0f, 110.0f,
                     {1700.0f, 900.0f});
        } else if (section == 16) {
            const f32 progress = ctx.progress();
            const f32 phase = progress * 6.28318530718f;
            scene.camera().enable(true)
                .position({std::sin(phase) * 120.0f,
                           std::cos(phase * 0.73f) * 80.0f,
                           -1350.0f + std::sin(phase * 0.61f) * 120.0f})
                .zoom(900.0f + std::cos(phase * 0.47f) * 70.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T23 is deliberately frame-addressable: the same semantic
            // scene must produce identical pixels when evaluated in any
            // order, including after camera and XYZ animation changes.
            add_text("t23_random_a", "RANDOM", {-260.0f, -155.0f, 0.0f},
                     {12.0f, -28.0f, -8.0f}, {0.58f, 0.58f, 1.0f});
            add_text("t23_random_b", "ACCESS", {220.0f, 20.0f, 80.0f},
                     {-18.0f, 34.0f, 11.0f}, {0.62f, 0.62f, 1.0f});
            add_text("t23_random_c", "DETERMINISTIC", {0.0f, 190.0f, -100.0f},
                     {8.0f, 12.0f, -16.0f}, {0.42f, 0.42f, 1.0f});
        } else if (section == 17) {
            const f32 phase = ctx.progress() * 6.28318530718f;
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1250.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T11: deliberately hostile CPU/executor workload. Every cell
            // is an independent TextRun with its own projected surface and
            // animated XYZ transform. The phase offset prevents accidental
            // temporal deduplication while the bounded grid keeps all 100
            // surfaces observable in one 1920x1080 frame.
            constexpr const char* kWords[] = {
                "AI", "CPU", "GPU", "DATA", "NEWS",
                "TECH", "2026", "FUTURE", "TEXT", "3D"
            };
            constexpr int kColumns = 10;
            constexpr int kRows = 10;
            for (int row = 0; row < kRows; ++row) {
                for (int column = 0; column < kColumns; ++column) {
                    const int index = row * kColumns + column;
                    const f32 cell_phase = phase + static_cast<f32>(index) * 0.113f;
                    const Vec3 position{
                        (static_cast<f32>(column) - 4.5f) * 175.0f,
                        (static_cast<f32>(row) - 4.5f) * 95.0f,
                        std::sin(cell_phase * 0.83f) * 260.0f};
                    const Vec3 rotation{
                        std::sin(cell_phase) * 18.0f,
                        std::cos(cell_phase * 1.07f) * 58.0f,
                        std::sin(cell_phase * 0.61f) * 12.0f};
                    const f32 scale = 0.24f + 0.018f * std::cos(cell_phase);
                    add_text("t11_stress100_" + std::to_string(index),
                             kWords[index % 10], position, rotation,
                             {scale, scale, 1.0f}, 0.88f, 92.0f,
                             {150.0f, 100.0f});
                }
            }
        } else if (section == 18) {
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1250.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T12: giant typography.  The 1800 px glyph deliberately
            // exercises large tight-surface rasterization while the layer
            // crosses near edge-on Y rotations and a 0.22x→1x→0.22x scale.
            // No manual canvas offset is used; the projected surface owns its
            // local origin and the camera owns the world-to-screen mapping.
            add_text("t12_giant", "AI", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 1.0f,
                     1800.0f, {1900.0f, 1000.0f});
        } else if (section == 19) {
            scene.camera().enable(true)
                .position({0.0f, 0.0f, -1250.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            // T13: scale explosion. Keep the authored surface bounded while
            // forcing the projected extent through 0.02x, 5x and back down.
            // This exposes unstable bbox math, pool fragmentation and
            // resampling quality without hiding failures behind a showcase
            // specific renderer shortcut.
            add_text("t13_scale", "EXPLOSION", {0.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 1.0f,
                     300.0f, {1800.0f, 420.0f});
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
    registry.add(make_composition_descriptor("Text25D-T08-ExtremePerspective", [](const CompositionProps&) {
        return text_25d_tests(5, "Text25D-T08-ExtremePerspective");
    }));
    registry.add(make_composition_descriptor("Text25D-T07-CameraFlyThrough", [](const CompositionProps&) {
        return text_25d_tests(6, "Text25D-T07-CameraFlyThrough");
    }));
    registry.add(make_composition_descriptor("Text25D-T14-TextTunnel", [](const CompositionProps&) {
        return text_25d_tests(7, "Text25D-T14-TextTunnel");
    }));
    registry.add(make_composition_descriptor("Text25D-T15-Helix3D", [](const CompositionProps&) {
        return text_25d_tests(8, "Text25D-T15-Helix3D");
    }));
    registry.add(make_composition_descriptor("Text25D-T16-Wall48", [](const CompositionProps&) {
        return text_25d_tests(9, "Text25D-T16-Wall48");
    }));
    registry.add(make_composition_descriptor("Text25D-T17-CameraOrbit", [](const CompositionProps&) {
        return text_25d_tests(10, "Text25D-T17-CameraOrbit");
    }));
    registry.add(make_composition_descriptor("Text25D-T18-DollyZoom", [](const CompositionProps&) {
        return text_25d_tests(11, "Text25D-T18-DollyZoom");
    }));
    registry.add(make_composition_descriptor("Text25D-T19-MotionBlur", [](const CompositionProps&) {
        return text_25d_tests(12, "Text25D-T19-MotionBlur");
    }));
    registry.add(make_composition_descriptor("Text25D-T20-DOFTypography", [](const CompositionProps&) {
        return text_25d_tests(13, "Text25D-T20-DOFTypography");
    }));
    registry.add(make_composition_descriptor("Text25D-T21-OpacityDepth", [](const CompositionProps&) {
        return text_25d_tests(14, "Text25D-T21-OpacityDepth");
    }));
    registry.add(make_composition_descriptor("Text25D-T22-LongText", [](const CompositionProps&) {
        return text_25d_tests(15, "Text25D-T22-LongText");
    }));
    registry.add(make_composition_descriptor("Text25D-T23-RandomAccess", [](const CompositionProps&) {
        return text_25d_tests(16, "Text25D-T23-RandomAccess");
    }));
    registry.add(make_composition_descriptor("Text25D-T11-Stress100", [](const CompositionProps&) {
        return text_25d_tests(17, "Text25D-T11-Stress100");
    }));
    registry.add(make_composition_descriptor("Text25D-T12-GiantTypography", [](const CompositionProps&) {
        return text_25d_tests(18, "Text25D-T12-GiantTypography");
    }));
    registry.add(make_composition_descriptor("Text25D-T13-ScaleExplosion", [](const CompositionProps&) {
        return text_25d_tests(19, "Text25D-T13-ScaleExplosion");
    }));
}

} // namespace chronon3d::content::two_point_five_d
