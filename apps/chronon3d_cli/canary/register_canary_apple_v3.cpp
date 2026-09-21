// ============================================================================
// apps/chronon3d_cli/canary/register_canary_apple_v3.cpp
//
// Family 5/6 of the canary pack: the Apple-like V3 choreography matrix.
//
// The same five-font fleet as the static baselines, with only the letter/word
// choreography changed — five motions × five fonts, every combination
// addressable as black_glow_<font>_<motion>.
//
// Apple-like V3 motion is deliberately a text-animation concern only: the
// scene, font size, centering, black background and glow here are the same
// contract as make_black_glow_static() (canary/register_canary_font_baselines.cpp).
// That keeps the font fleet useful as a visual baseline while the glyph/word
// choreography can evolve independently.
// ============================================================================

#include "canary_common.hpp"
#include "register_canary.hpp"

#include <array>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <string>

namespace chronon3d {
namespace {

    using namespace canary_detail;

    enum class AppleV3Motion {
        PremiumSoftReveal,
        PrecisionWordStagger,
        MaskedVerticalLift,
        HeroScaleFocus,
        KineticKeywordLock,
    };

    [[nodiscard]] GlyphSelectorSpec apple_v3_selector(
        const char *id, TextSelectorUnit unit, TextSelectorOrder order) {
        GlyphSelectorSpec selector;
        selector.id = id;
        selector.unit = unit;
        selector.shape = TextSelectorShape::Square;
        selector.order = order;
        selector.start = 0.0f;
        selector.start.add_keyframe(Frame{0}, 0.0f,
                                    EasingCurve{Easing::Linear});
        selector.start.add_keyframe(Frame{60}, 100.0f,
                                    EasingCurve{Easing::OutCubic});
        selector.end = 100.0f;
        selector.exclude_spaces = true;
        return selector;
    }

    [[nodiscard]] Composition make_black_glow_apple_v3(
        const char *name, const char *font_path, f32 font_size,
        AppleV3Motion motion) {
        return composition(
            black_glow_spec(name), [font_path, font_size, motion](
                const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                    l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                });

                scene.layer("CHRONON_TEXT", [&](LayerBuilder &l) {
                    const bool word_motion =
                        motion == AppleV3Motion::PrecisionWordStagger ||
                        motion == AppleV3Motion::MaskedVerticalLift ||
                        motion == AppleV3Motion::KineticKeywordLock;
                    TextDefinition definition = canary_text(
                        word_motion ? "CHRONON LAB" : "CHRONON",
                        font_path, font_size, 700,
                        Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f});

                    const TextSelectorUnit unit =
                        (motion == AppleV3Motion::PremiumSoftReveal ||
                         motion == AppleV3Motion::HeroScaleFocus)
                            ? TextSelectorUnit::Glyph
                            : TextSelectorUnit::Word;
                    const TextSelectorOrder order =
                        (motion == AppleV3Motion::HeroScaleFocus ||
                         motion == AppleV3Motion::KineticKeywordLock)
                            ? TextSelectorOrder::FromCenter
                            : TextSelectorOrder::Forward;

                    TextAnimatorSpec reveal;
                    reveal.id = "apple_v3_reveal";
                    reveal.selectors.push_back(
                        apple_v3_selector("apple_v3_reveal_selector", unit, order));
                    reveal.properties.push_back(OpacityProperty{0.0f});
                    reveal.transform_mode = TextPropertyBlendMode::Multiply;
                    definition.animation.animators.push_back(std::move(reveal));

                    TextAnimatorSpec motion_animator;
                    motion_animator.id = "apple_v3_motion";
                    motion_animator.selectors.push_back(
                        apple_v3_selector("apple_v3_motion_selector", unit, order));

                    switch (motion) {
                    case AppleV3Motion::PremiumSoftReveal: {
                        PositionProperty position;
                        position.value.add_keyframe(
                            Frame{0}, Vec3{0.0f, 10.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        position.value.add_keyframe(
                            Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(position));

                        ScaleProperty scale;
                        scale.value.add_keyframe(
                            Frame{0}, Vec3{0.97f, 0.97f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        scale.value.add_keyframe(
                            Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(scale));

                        BlurProperty blur;
                        blur.radius.add_keyframe(
                            Frame{0}, 14.0f, EasingCurve{Easing::OutCubic});
                        blur.radius.add_keyframe(
                            Frame{60}, 0.0f, EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(blur));
                        break;
                    }
                    case AppleV3Motion::PrecisionWordStagger: {
                        PositionProperty position;
                        position.value.add_keyframe(
                            Frame{0}, Vec3{0.0f, 14.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        position.value.add_keyframe(
                            Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(position));

                        ScaleProperty scale;
                        scale.value.add_keyframe(
                            Frame{0}, Vec3{0.985f, 0.985f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        scale.value.add_keyframe(
                            Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(scale));
                        break;
                    }
                    case AppleV3Motion::MaskedVerticalLift: {
                        PositionProperty position;
                        position.value.add_keyframe(
                            Frame{0}, Vec3{0.0f, 36.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        position.value.add_keyframe(
                            Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(position));
                        break;
                    }
                    case AppleV3Motion::HeroScaleFocus: {
                        ScaleProperty scale;
                        scale.value.add_keyframe(
                            Frame{0}, Vec3{1.08f, 1.08f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        scale.value.add_keyframe(
                            Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(scale));

                        BlurProperty blur;
                        blur.radius.add_keyframe(
                            Frame{0}, 10.0f, EasingCurve{Easing::OutCubic});
                        blur.radius.add_keyframe(
                            Frame{60}, 0.0f, EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(blur));
                        break;
                    }
                    case AppleV3Motion::KineticKeywordLock: {
                        PositionProperty position;
                        position.value.add_keyframe(
                            Frame{0}, Vec3{0.0f, 12.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        position.value.add_keyframe(
                            Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(position));

                        ScaleProperty scale;
                        scale.value.add_keyframe(
                            Frame{0}, Vec3{0.96f, 0.96f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        scale.value.add_keyframe(
                            Frame{30}, Vec3{1.04f, 1.04f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        scale.value.add_keyframe(
                            Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                            EasingCurve{Easing::OutCubic});
                        motion_animator.properties.push_back(std::move(scale));
                        break;
                    }
                    }

                    definition.animation.animators.push_back(std::move(motion_animator));
                    definition.animation.duration = kBlackGlowDuration;
                    definition.animation.cache_layout = true;
                    l.text("text", std::move(definition));

                    // Identical glow contract to make_black_glow_static():
                    // motion is the only variable in this baseline.
                    l.effect(GlowPresets::canary_black());
                });

                scene.camera().set(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                return scene.build();
            });
    }

    struct AppleV3Font {
        const char *suffix;
        const char *path;
    };
    struct AppleV3MotionEntry {
        const char *suffix;
        AppleV3Motion motion;
    };

    constexpr std::array<AppleV3Font, 5> kAppleV3Fonts{{
        {"inter", "assets/fonts/Inter-Bold.ttf"},
        {"poppins", "assets/fonts/Poppins-Bold.ttf"},
        {"dmsans", "assets/fonts/DMSans-Bold.ttf"},
        {"georgia", "assets/fonts/Georgia_Bold.ttf"},
        {"freeserif", "assets/fonts/FreeSerif.ttf"},
    }};
    constexpr std::array<AppleV3MotionEntry, 5> kAppleV3Motions{{
        {"premium_soft_reveal", AppleV3Motion::PremiumSoftReveal},
        {"precision_word_stagger", AppleV3Motion::PrecisionWordStagger},
        {"masked_vertical_lift", AppleV3Motion::MaskedVerticalLift},
        {"hero_scale_focus", AppleV3Motion::HeroScaleFocus},
        {"kinetic_keyword_lock", AppleV3Motion::KineticKeywordLock},
    }};

} // namespace

void register_canary_apple_v3_compositions(CompositionRegistry &registry) {

    // The five-font × five-motion matrix, opened out explicitly so every
    // render is addressable and the static baselines stay untouched.
    for (const auto &font : kAppleV3Fonts) {
        for (const auto &entry : kAppleV3Motions) {
            const std::string id = std::string{"black_glow_"} + font.suffix +
                                   "_" + entry.suffix;
            registry.add(make_composition_descriptor(
                id,
                [id, font_path = font.path, motion = entry.motion] {
                    return make_black_glow_apple_v3(
                        id.c_str(), font_path, 190.0f, motion);
                }));
        }
    }
}

} // namespace chronon3d
