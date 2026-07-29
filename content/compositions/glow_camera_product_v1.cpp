#include "content/compositions/glow_camera_product_v1.hpp"

#include "content/common/text/cinematic_glow.hpp"

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/text/text_definition.hpp>

namespace chronon3d::content::product {
namespace {

camera_v1::CameraDescriptor product_camera() {
    camera_v1::CameraDescriptor descriptor;
    descriptor.id = "glow_camera_product_v1_orbit";
    descriptor.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    descriptor.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    descriptor.base.projection = camera_v1::ZoomProjection{
        AnimatedValue<float>{1000.0f}};
    descriptor.base.point_of_interest_enabled = true;
    descriptor.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};

    camera_v1::OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.key(Frame{0}, 0.0f).key(Frame{60}, 90.0f);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);
    descriptor.source = orbit;
    descriptor.orientation = camera_v1::LookAtPoint{
        Vec3{0.0f, 0.0f, 0.0f}};
    return descriptor;
}

} // namespace

chronon3d::Composition make_glow_camera_product_v1() {
    auto result = chronon3d::composition(
        {
            .name = "GlowCameraProductV1",
            .width = 1920,
            .height = 1080,
            .frame_rate = FrameRate{30, 1},
            .duration = Frame{60},
        },
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            if (ctx.font_engine) scene.font_engine(ctx.font_engine);
            AnimatedValue<f32> title_opacity{0.0f};
            title_opacity.key(Frame{0}, 0.0f).key(Frame{8}, 1.0f);
            const f32 opacity = title_opacity.evaluate(ctx.local_time());

            scene.layer("background", [](LayerBuilder& layer) {
                layer.grid_background("grid", GridBackgroundParams{
                    .size = {1920.0f, 1080.0f},
                    .bg_color = {0.015f, 0.02f, 0.06f, 1.0f},
                    .grid_color = {0.20f, 0.42f, 1.0f, 0.12f},
                    .spacing = 80.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true,
                });
            });

            scene.layer("parallax_back", [](LayerBuilder& layer) {
                layer.enable_3d().position({360.0f, 300.0f, 600.0f});
                layer.rounded_rect("card", {
                    .size = {360.0f, 220.0f},
                    .radius = 24.0f,
                    .color = {0.08f, 0.18f, 0.48f, 0.82f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid({0.08f, 0.18f, 0.48f, 0.82f}),
                });
            });

            scene.layer("glow_text", [opacity](LayerBuilder& layer) {
                layer.position({180.0f, 35.0f, 0.0f});
                layer.text("title", TextDefinition{
                    .content = {.value = "GLOW CAMERA"},
                    .style = {
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 104.0f,
                        },
                        .color = Color::white(),
                    },
                    .frame = {
                        .size = {1080.0f, 160.0f},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute, {0.0f, 0.0f}},
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                    },
                });
                layer.opacity(opacity);
                text_reveal::apply_cinematic_glow(layer,
                    text_reveal::CinematicGlowPreset{
                        .inner_radius = 4.0f,
                        .mid_radius = 14.0f,
                        .bloom_radius = 34.0f,
                        .inner_intensity = 0.55f,
                        .mid_intensity = 0.22f,
                        .bloom_intensity = 0.08f,
                    });
            });

            scene.layer("parallax_front", [](LayerBuilder& layer) {
                layer.enable_3d().position({1300.0f, 700.0f, -300.0f});
                layer.circle("orb", {
                    .radius = 92.0f,
                    .color = {0.20f, 0.82f, 1.0f, 0.92f},
                    .pos = {0.0f, 0.0f, 0.0f},
                });
                layer.effect(GlowParams{
                    .radius = 24.0f,
                    .intensity = 0.55f,
                    .color = {0.20f, 0.82f, 1.0f, 1.0f},
                });
            });

            return scene.build();
        });
    result.default_camera_descriptor(product_camera());
    return result;
}

} // namespace chronon3d::content::product
