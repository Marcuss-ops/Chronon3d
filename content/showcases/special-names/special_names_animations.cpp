#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/text/text_definition.hpp>

#include <functional>
#include <utility>

#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"   // M1.8 §2D — kept for typewriter_text() in special_name_typewriter()
#include "special_names_theme.hpp"

namespace chronon3d::content::special_names {

using namespace chronon3d::content::animation_helpers;
using namespace chronon3d::content;

constexpr Frame kIntro = 22;

namespace {

// ── make_special_name_comp ──────────────────────────────────────────────────
// Shared helper for the 6 standard SpecialName compositions.  Wraps the common
// skeleton (black bg, centered layer, text) and lets each composition supply
// only its unique animation keyframes via a setup lambda.
//
// Previously each composition duplicated ~18 lines of identical boilerplate;
// now they are 8–12 lines of pure animation data.
using AnimSetup = std::function<void(LayerBuilder&)>;

Composition make_special_name_comp(const char* name, AnimSetup setup) {
    return composition({.name = name, .width = 1920, .height = 1080, .duration = 60},
        [setup = std::move(setup)](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_black_background(s);
            s.layer("name", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l);
                // Keep text styling explicit and effects disabled for stable animated bounds.
                auto name_text = TextDefinition{
    .content = {.value = DEMO_NAME},
    .style = {
        .font = {
            .font_path = "assets/fonts/Poppins-Bold.ttf",
            .font_family = "Poppins",
            .font_weight = 700,
            .font_size = 110.0f,
        },
        .color = NAME_TEXT,
    },
    .frame = {
        .tracking = 14.0f
    }
                };
                l.text("name", std::move(name_text));
            });
            return s.build();
        });
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. FadeUp — name rises into place.
Composition special_name_fade_up() {
    return make_special_name_comp("SpecialNameFadeUp", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0}, Vec3{0.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 2. SlideLeft — name enters from the left.
Composition special_name_slide_left() {
    return make_special_name_comp("SpecialNameSlideLeft", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0}, Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 3. SlideRight — name enters from the right.
Composition special_name_slide_right() {
    return make_special_name_comp("SpecialNameSlideRight", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0}, Vec3{60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 4. ScaleIn — name scales from 0.85 to 1.0.
Composition special_name_scale_in() {
    return make_special_name_comp("SpecialNameScaleIn", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.scale_anim()
            .key(Frame{0}, Vec3{0.85f, 0.85f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 5. Stamp — OutBack overshoot, settles at 1.0 by frame 22.
Composition special_name_stamp() {
    return make_special_name_comp("SpecialNameStamp", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutExpo})
            .key(Frame{12}, 1.0f, EasingCurve{Easing::Linear});
        l.scale_anim()
            .key(Frame{0}, Vec3{0.70f, 0.70f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{kIntro}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutBack});
    });
}

// 6. BlurIn — name fades in while a blur clears.
Composition special_name_blur_in() {
    return make_special_name_comp("SpecialNameBlurIn", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.blur_anim()
            .key(Frame{0}, 3.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 0.0f, EasingCurve{Easing::OutCubic});
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Typewriter — stable text reveal for the compiled render path.
// ═════════════════════════════════════════════════════════════════════════════
Composition special_name_typewriter() {
    return composition({.name="SpecialNameTypewriter", .width=1920, .height=1080, .duration=60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("tw", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 1.0f, EasingCurve{Easing::OutCubic});
            l.text("name", chronon3d::TextDefinition{
                .content = {.value = DEMO_NAME},
                .style = {.font = {.font_path = "assets/fonts/Poppins-Bold.ttf",
                                   .font_family = "Poppins",
                                   .font_weight = 700,
                                   .font_size = 110.0f},
                          .color = NAME_TEXT_GOLD},
                .frame = {.size = {1200.0f, 240.0f},
                          .anchor = chronon3d::TextAnchor::Center,
                          .align = chronon3d::TextAlign::Center,
                          .vertical_align = chronon3d::VerticalAlign::Middle,
                          .tracking = 14.0f},
            });
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// (Note: the previous SpecialNameRole* / RolePreset family has been moved
// to a dedicated content/ImportantWords/ category — palettes + presets live
// in important_words_theme.hpp there.)
// ═════════════════════════════════════════════════════════════════════════════

// ── Per-domain registration ──────────────────────────────────────────────────
void register_special_name_compositions(CompositionRegistry& registry) {
    const auto add = [&registry](const char* id, std::function<Composition(const CompositionProps&)> factory) {
        registry.add(make_composition_descriptor(
            CompositionDescriptor{.id = id, .category = std::string{content_category::NamedText}},
            std::move(factory)));
    };
    add("SpecialNameFadeUp", [](const CompositionProps&) { return special_name_fade_up(); });
    add("SpecialNameSlideLeft", [](const CompositionProps&) { return special_name_slide_left(); });
    add("SpecialNameSlideRight", [](const CompositionProps&) { return special_name_slide_right(); });
    add("SpecialNameScaleIn", [](const CompositionProps&) { return special_name_scale_in(); });
    add("SpecialNameStamp", [](const CompositionProps&) { return special_name_stamp(); });
    add("SpecialNameBlurIn", [](const CompositionProps&) { return special_name_blur_in(); });
    add("SpecialNameTypewriter", [](const CompositionProps&) { return special_name_typewriter(); });
}

} // namespace chronon3d::content::special_names
