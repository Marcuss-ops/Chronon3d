#pragma once
// ============================================================================
// apps/chronon3d_cli/canary/canary_text_animation.hpp
//
// The canary text-animation kit: the definitions that put Chronon3D's own
// shaped/layout-owned text nodes under animation.  Both helpers keep one
// complete TextDefinition per layer — no substring, no second layout, no
// per-glyph advance rebuilt in the composition layer.
//
// Included by the text-contract canaries, the Final3D typewriter catalog entry
// and the typewriter font-fleet card, which is why it lives beside the common
// kit instead of inside one subfile.
// ============================================================================

#include "canary_common.hpp"

#include <chronon3d/text/text_definition.hpp>
#include <string>
#include <utility>

namespace chronon3d::canary_detail {

    /// Full-text typewriter definition.  Chronon3D shapes the complete source
    /// string first; the animator only changes opacity on Chronon's stable
    /// grapheme elements.  No substring, per-letter advance or second text
    /// layout is created by the composition.
    [[nodiscard]] inline TextDefinition canary_typewriter_text(
        std::string value, const char *font_path, f32 font_size,
        Color color, Vec2 box) {

        auto definition = canary_text(
            std::move(value), font_path, font_size, 700, color, box);

        GlyphSelectorSpec hidden_clusters;
        hidden_clusters.id = "typewriter_hidden_clusters";
        hidden_clusters.unit = TextSelectorUnit::Grapheme;
        hidden_clusters.shape = TextSelectorShape::Square;
        hidden_clusters.order = TextSelectorOrder::Forward;
        hidden_clusters.start = 0.0f;
        hidden_clusters.start.add_keyframe(Frame{0}, 0.0f);
        hidden_clusters.start.add_keyframe(
            kTypewriterRevealEnd, 100.0f, EasingCurve{Easing::OutCubic});
        hidden_clusters.end = 100.0f;
        hidden_clusters.exclude_spaces = true;

        TextAnimatorSpec reveal;
        reveal.id = "typewriter_reveal_full_text";
        reveal.selectors.push_back(std::move(hidden_clusters));
        reveal.properties.push_back(OpacityProperty{0.0f});
        reveal.transform_mode = TextPropertyBlendMode::Multiply;

        definition.animation.animators.push_back(std::move(reveal));
        definition.animation.duration = kTypewriterRevealEnd;
        definition.animation.cache_layout = true;
        return definition;
    }

    enum class ElementCanaryMode {
        Static,
        Opacity,
        Motion,
    };

    // A canary for the Chronon text-element contract.  The text remains
    // one complete TextDefinition and therefore one shaped/layout-owned
    // node.  "Elements" here are Chronon's internal glyph states: this
    // helper never creates a second text run and never reconstructs
    // advances in the composition layer.
    [[nodiscard]] inline TextDefinition canary_element_text(
        std::string value, ElementCanaryMode mode) {
        auto definition = canary_text(
            std::move(value), kCanaryFontBold, 180.0f, 700,
            Color{1.0f, 0.86f, 0.86f, 1.0f}, Vec2{1600.0f, 360.0f});

        GlyphSelectorSpec selector;
        selector.id = "chronon_elements";
        selector.unit = TextSelectorUnit::Grapheme;
        selector.shape = TextSelectorShape::Square;
        selector.order = TextSelectorOrder::Forward;
        selector.start = 0.0f;
        selector.end = 100.0f;
        selector.exclude_spaces = true;

        TextAnimatorSpec animator;
        animator.id = "chronon_elements_static_or_reveal";
        animator.selectors.push_back(selector);

        if (mode == ElementCanaryMode::Static) {
            // B: selecting every shaped element with an identity visual
            // property must be pixel-equivalent to the plain full run.
            animator.properties.push_back(OpacityProperty{1.0f});
        } else {
            // C/D: hide the still-unrevealed shaped elements.  The
            // selector boundary is animated; no glyph position is
            // invented here.
            selector.start = 0.0f;
            selector.start.add_keyframe(Frame{0}, 0.0f,
                                        EasingCurve{Easing::Linear});
            selector.start.add_keyframe(Frame{42}, 100.0f,
                                        EasingCurve{Easing::OutCubic});
            animator.selectors.clear();
            animator.selectors.push_back(std::move(selector));
            animator.properties.push_back(OpacityProperty{0.0f});
            animator.transform_mode = TextPropertyBlendMode::Multiply;
        }

        definition.animation.animators.push_back(std::move(animator));

        if (mode == ElementCanaryMode::Motion) {
            // D: the same shaped elements now receive only motion deltas.
            // The base positions/advances still come from Chronon's
            // layout.  Layer-level 3D rotation remains the camera-facing
            // rotation; per-element Z/scale are evaluated here.
            GlyphSelectorSpec motion_selector;
            motion_selector.id = "chronon_elements_motion";
            motion_selector.unit = TextSelectorUnit::Grapheme;
            motion_selector.shape = TextSelectorShape::Square;
            motion_selector.start = 0.0f;
            motion_selector.end = 100.0f;

            TextAnimatorSpec motion;
            motion.id = "chronon_elements_motion_delta";
            motion.selectors.push_back(std::move(motion_selector));

            PositionProperty position;
            position.value.add_keyframe(Frame{0}, Vec3{0.0f, -24.0f, -20.0f},
                                        EasingCurve{Easing::OutCubic});
            position.value.add_keyframe(Frame{42}, Vec3{0.0f, 0.0f, 0.0f},
                                        EasingCurve{Easing::OutCubic});
            motion.properties.push_back(std::move(position));

            ScaleProperty scale;
            scale.value.add_keyframe(Frame{0}, Vec3{0.82f, 0.82f, 1.0f},
                                     EasingCurve{Easing::OutCubic});
            scale.value.add_keyframe(Frame{42}, Vec3{1.0f, 1.0f, 1.0f},
                                     EasingCurve{Easing::OutCubic});
            motion.properties.push_back(std::move(scale));

            definition.animation.animators.push_back(std::move(motion));
        }

        return definition;
    }

} // namespace chronon3d::canary_detail
