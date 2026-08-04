#include <content/launches/light_transition_sound_smoke.hpp>

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/clip_transition.hpp>
#include <chronon3d/text/text_definition.hpp>

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

        ClipTransitionSpec transition;
        transition.kind = ClipTransitionKind::Flash;
        transition.easing = Easing::InOutCubic;
        transition.flash_color = flash_color;
        scene.clip_transition(
            "scene_a",
            "scene_b",
            transition,
            Frame{20},
            Frame{12});

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
