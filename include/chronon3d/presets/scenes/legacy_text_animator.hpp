#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// legacy_text_animator.hpp — compatibility shim for presets/bench_corpus
//
// The presets and benchmark corpus were written against a short-lived
// `TextAnimator` façade that never shipped and was removed with the rest of
// the authoring facade (commit 4850746c2, "cleanup: remove legacy authoring
// text facade").  This header re-maps the same fluent surface onto the
// canonical authoring path:
//
//   LayerBuilder::text(name, TextDefinition)  +  TextDefinition::animation
//   (TextAnimatorSpec / GlyphSelectorSpec / TextAnimatorProperty)
//
// It is intentionally scoped to the presets directory and is NOT part of the
// public SDK surface.  Per the Chronon migration rule, this bridge exists
// only while presets/bench_corpus consume it; the exit condition is a
// mechanical rewrite of those call sites against the canonical DTOs.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>

#include <string>
#include <utility>

namespace chronon3d {

enum class TextAnimMode {
    ByCharacter,
    ByWord,
    ByLine
};

struct TextAnimatorConfig {
    TextAnimMode mode = TextAnimMode::ByCharacter;
    Frame duration{25};
    Frame delay_per_unit{3};
    Easing easing = Easing::OutCubic;
    bool animate_opacity = false;
    bool animate_slide = false;
    Vec3 slide_from{0.0f, 0.0f, 0.0f};
    bool animate_scale = false;
    Vec3 scale_from{1.0f, 1.0f, 1.0f};
    bool animate_tracking = false;
    f32 tracking_from = 0.0f;
};

class TextAnimator {
public:
    TextAnimator& text(std::string value) {
        text_ = std::move(value);
        return *this;
    }
    TextAnimator& font_size(f32 size) {
        font_size_ = size;
        return *this;
    }
    TextAnimator& font_path(std::string path) {
        font_path_ = std::move(path);
        return *this;
    }
    TextAnimator& font_weight(int weight) {
        font_weight_ = weight;
        return *this;
    }
    TextAnimator& color(Color c) {
        color_ = c;
        return *this;
    }
    TextAnimator& align(TextAlign a) {
        align_ = a;
        return *this;
    }
    TextAnimator& config(TextAnimatorConfig c) {
        config_ = std::move(c);
        return *this;
    }

    void build(SceneBuilder& s, const std::string& layer_name) {
        s.layer(layer_name, [this, &layer_name](LayerBuilder& builder) {
            TextDefinition definition;
            definition.content.value = text_;
            definition.style.font.font_path = font_path_;
            definition.style.font.font_size = font_size_;
            definition.style.font.font_weight = font_weight_;
            definition.style.color = color_;
            definition.frame.size = {1920.0f, 1080.0f};
            definition.frame.placement =
                TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}};
            definition.frame.anchor = TextAnchor::Center;
            definition.frame.align = align_;
            definition.frame.vertical_align = VerticalAlign::Middle;

            TextAnimatorSpec spec;
            spec.id = layer_name + "_legacy_animator";

            GlyphSelectorSpec selector;
            selector.id = spec.id + "_selector";
            selector.unit = selector_unit(config_.mode);
            selector.order = TextSelectorOrder::Forward;
            spec.selectors.push_back(std::move(selector));

            const Frame f0{0};
            const Frame f1 = config_.duration;
            if (config_.animate_opacity) {
                OpacityProperty property;
                property.value.clear();
                property.value.add_keyframe(f0, 0.0f);
                property.value.add_keyframe(f1, 1.0f, EasingCurve{config_.easing});
                spec.properties.push_back(std::move(property));
            }
            if (config_.animate_slide) {
                PositionProperty property;
                property.value.clear();
                property.value.add_keyframe(f0, config_.slide_from);
                property.value.add_keyframe(f1, Vec3{0.0f, 0.0f, 0.0f},
                                            EasingCurve{config_.easing});
                spec.properties.push_back(std::move(property));
            }
            if (config_.animate_scale) {
                ScaleProperty property;
                property.value.clear();
                property.value.add_keyframe(f0, config_.scale_from);
                property.value.add_keyframe(f1, Vec3{1.0f, 1.0f, 1.0f},
                                            EasingCurve{config_.easing});
                spec.properties.push_back(std::move(property));
            }
            if (config_.animate_tracking) {
                TrackingProperty property;
                property.pixels.clear();
                property.pixels.add_keyframe(f0, config_.tracking_from);
                property.pixels.add_keyframe(f1, 0.0f, EasingCurve{config_.easing});
                spec.properties.push_back(std::move(property));
            }

            definition.animation.animators.push_back(std::move(spec));
            definition.animation.start_delay = Frame{0};
            definition.animation.duration = config_.duration;

            builder.text(layer_name, std::move(definition));
        });
    }

private:
    static TextSelectorUnit selector_unit(TextAnimMode mode) noexcept {
        switch (mode) {
            case TextAnimMode::ByWord: return TextSelectorUnit::Word;
            case TextAnimMode::ByLine: return TextSelectorUnit::Line;
            case TextAnimMode::ByCharacter: break;
        }
        return TextSelectorUnit::Character;
    }

    std::string text_;
    f32 font_size_ = 48.0f;
    std::string font_path_;
    int font_weight_ = 400;
    Color color_{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align_ = TextAlign::Center;
    TextAnimatorConfig config_{};
};

} // namespace chronon3d
