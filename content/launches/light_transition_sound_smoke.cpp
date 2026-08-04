#include <content/launches/light_transition_sound_smoke.hpp>

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/clip_transition.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <string>

namespace chronon3d::content::launches {

namespace {

TextDefinition scene_label(const char* value) {
    return TextDefinition{
        .content = {.value = value},
        .style = {
            .font = {
                .font_path = "tests/fixtures/Inter-Bold.ttf",
                .font_size = 132.0f,
            },
            .color = Color::white(),
        },
        .frame = {
            .size = {1500.0f, 240.0f},
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .tracking = 8.0f,
        },
    };
}

} // namespace

namespace {

Composition make_light_transition_variant(const char* name, const char* scene_b_label,
                                          Color flash_color) {
    return composition({
        .name = name,
        .width = 1920,
        .height = 1080,
        .frame_rate = FrameRate{30, 1},
        .duration = Frame{60},
    }, [scene_b_label, flash_color](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        const bool has_runtime_font_engine = ctx.runtime != nullptr;
        if (has_runtime_font_engine) scene.font_engine(&ctx.runtime->font_engine());

        scene.layer("scene_a", [has_runtime_font_engine](LayerBuilder& layer) {
            layer.rect("scene_a_background", {
                .size = {1920.0f, 1080.0f},
                .color = {0.03f, 0.08f, 0.22f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
            if (has_runtime_font_engine) {
                layer.pin_to(Anchor::Center);
                layer.text("scene_a_label", scene_label("SCENE A"));
            }
        });

        scene.layer("scene_b", [has_runtime_font_engine, scene_b_label](LayerBuilder& layer) {
            layer.rect("scene_b_background", {
                .size = {1920.0f, 1080.0f},
                .color = {0.28f, 0.03f, 0.04f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
            if (has_runtime_font_engine) {
                layer.pin_to(Anchor::Center);
                layer.text("scene_b_label", scene_label(scene_b_label));
            }
        });

        // The transition node performs a fast dissolve. These lightweight
        // animated layers create the actual cinematic light leak on top.
        ClipTransitionSpec transition;
        transition.kind = ClipTransitionKind::LightLeak;
        transition.easing = Easing::InOutCubic;
        transition.flash_color = flash_color;
        scene.clip_transition("scene_a", "scene_b", transition, Frame{20}, Frame{12});

        for (int i = 0; i < 3; ++i) {
            const f32 y = -470.0f + static_cast<f32>(i) * 420.0f;
            const f32 width = 1280.0f - static_cast<f32>(i) * 220.0f;
            const f32 angle = -24.0f + static_cast<f32>(i) * 7.0f;
            scene.layer("light_leak_band_" + std::to_string(i),
                [flash_color, y, width, angle, i](LayerBuilder& layer) {
                    layer.position_anim()
                        .key(Frame{0},  Vec3{-1500.0f, y, 0.0f}, EasingCurve{Easing::Hold})
                        .key(Frame{19}, Vec3{-1500.0f, y, 0.0f}, EasingCurve{Easing::Hold})
                        .key(Frame{23}, Vec3{-550.0f, y, 0.0f}, EasingCurve{Easing::OutCubic})
                        .key(Frame{28}, Vec3{ 650.0f, y, 0.0f}, EasingCurve{Easing::InOutCubic})
                        .key(Frame{35}, Vec3{1800.0f, y, 0.0f}, EasingCurve{Easing::InCubic})
                        .key(Frame{60}, Vec3{1800.0f, y, 0.0f}, EasingCurve{Easing::Hold});
                    layer.rotate({0.0f, 0.0f, angle});
                    layer.opacity_anim()
                        .key(Frame{0},  0.0f, EasingCurve{Easing::Hold})
                        .key(Frame{20}, 0.0f, EasingCurve{Easing::Hold})
                        .key(Frame{24}, i == 1 ? 0.76f : 0.42f, EasingCurve{Easing::OutCubic})
                        .key(Frame{28}, i == 1 ? 0.52f : 0.25f, EasingCurve{Easing::InOutCubic})
                        .key(Frame{32}, 0.0f, EasingCurve{Easing::InCubic})
                        .key(Frame{60}, 0.0f, EasingCurve{Easing::Hold});
                    layer.blend(BlendMode::Screen);
                    layer.rect("streak", {
                        .size = {width, i == 1 ? 150.0f : 92.0f},
                        .color = flash_color.with_alpha(0.92f),
                        .fill = FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, {
                            {0.0f, flash_color.with_alpha(0.0f)},
                            {0.28f, flash_color.with_alpha(0.38f)},
                            {0.50f, Color::white().with_alpha(0.94f)},
                            {0.72f, flash_color.with_alpha(0.38f)},
                            {1.0f, flash_color.with_alpha(0.0f)},
                        }),
                    });
                });
        }

        scene.layer("light_leak_flare", [flash_color](LayerBuilder& layer) {
            layer.position({0.0f, 0.0f, 0.0f});
            layer.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::Hold})
                .key(Frame{23}, 0.0f, EasingCurve{Easing::Hold})
                .key(Frame{26}, 0.62f, EasingCurve{Easing::OutCubic})
                .key(Frame{32}, 0.0f, EasingCurve{Easing::InCubic})
                .key(Frame{60}, 0.0f, EasingCurve{Easing::Hold});
            layer.blend(BlendMode::Screen);
            layer.circle("flare", {
                .radius = 210.0f,
                .color = Color::white().with_alpha(0.85f),
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = FillStyle::radial({0.5f, 0.5f}, 0.5f, {
                    {0.0f, Color::white().with_alpha(0.86f)},
                    {0.42f, flash_color.with_alpha(0.46f)},
                    {1.0f, flash_color.with_alpha(0.0f)},
                }),
            });
        });

        return scene.build();
    });
}

} // namespace

Composition light_transition_sound_smoke() {
    return make_light_transition_variant("LightTransitionSoundSmoke", "SCENE B",
                                         Color::white());
}

Composition light_transition_orange_flash() {
    return make_light_transition_variant("LightTransitionOrangeFlash", "ORANGE FLASH",
                                         Color{1.0f, 0.22f, 0.015f, 1.0f});
}

Composition light_transition_amber_flash() {
    return make_light_transition_variant("LightTransitionAmberFlash", "AMBER FLASH",
                                         Color{1.0f, 0.48f, 0.02f, 1.0f});
}

Composition light_transition_copper_flash() {
    return make_light_transition_variant("LightTransitionCopperFlash", "COPPER FLASH",
                                         Color{0.88f, 0.12f, 0.015f, 1.0f});
}

} // namespace chronon3d::content::launches
