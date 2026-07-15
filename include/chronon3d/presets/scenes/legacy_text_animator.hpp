#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// legacy_text_animator.hpp — compatibility shim for presets/bench_corpus
//
// The presets and benchmark corpus were written against a short-lived
// `TextAnimator` façade that never shipped. This header provides a drop-in
// replacement that maps the same surface onto the canonical authoring API:
//   authoring::Layer / authoring::Text / authoring::Animator / Selector.
//
// It is intentionally scoped to the presets directory and is NOT part of the
// public SDK surface.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/selector.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/text_placement.hpp>

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
        s.layer(layer_name, [this](LayerBuilder& builder) {
            authoring::Layer layer(builder, CanvasInfo::from_dimensions(1920.0f, 1080.0f));
            auto& t = layer.text(text_)
                           .font(font_path_, font_size_)
                           .weight(font_weight_)
                           .color(color_)
                           .align(align_)
                           .anchor_point(TextAnchor::Center)
                           .vertical_align(VerticalAlign::Middle);

            TextSelectorUnit unit = TextSelectorUnit::Character;
            if (config_.mode == TextAnimMode::ByWord) {
                unit = TextSelectorUnit::Word;
            } else if (config_.mode == TextAnimMode::ByLine) {
                unit = TextSelectorUnit::Line;
            }

            authoring::Animator anim("legacy");
            anim.select(authoring::selector(unit)
                            .start(Frame{0}, 0.0f)
                            .end(config_.duration, 100.0f, config_.easing));

            if (config_.animate_opacity) {
                anim.opacity(0.0f);
            }
            if (config_.animate_slide) {
                anim.position(config_.slide_from);
            }
            if (config_.animate_scale) {
                anim.scale(config_.scale_from);
            }
            if (config_.animate_tracking) {
                anim.tracking(config_.tracking_from);
            }

            t.animate(std::move(anim));
        });
    }

private:
    std::string text_;
    f32 font_size_ = 48.0f;
    std::string font_path_;
    int font_weight_ = 400;
    Color color_{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align_ = TextAlign::Center;
    TextAnimatorConfig config_{};
};

} // namespace chronon3d
