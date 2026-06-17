#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"

#include <functional>
#include <algorithm>

namespace chronon3d::content::anims {

using namespace chronon3d::content::animation_helpers;

namespace {

// ── make_basic_anim ────────────────────────────────────────────────────────
// Shared helper for the 3 basic text animation compositions (FadeInText,
// SlideText, ScaleText).  Encapsulates the common skeleton (black bg, centered
// layer, drop shadow, 64pt text) and lets each composition supply only its
// animation-specific setup via a lambda.
//
// Previously each composition duplicated ~14 lines of identical boilerplate;
// now they are 4–7 lines of pure animation logic.
using BasicAnimSetup = std::function<void(LayerBuilder& l, const FrameContext& ctx)>;

Composition make_basic_anim(const char* name, const char* text, BasicAnimSetup setup) {
    return composition({.name = name, .width = 1920, .height = 1080, .duration = 60},
        [text, setup = std::move(setup)](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_black_background(s);
            s.layer("text", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l, ctx);
                l.drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
                 .text("label", make_text(text, 64.0f));
            });
            return s.build();
        });
}

} // namespace

// ── AnimFadeInText: text fades in smoothly ──────────────────────────────
Composition anim_fade_in_text() {
    return make_basic_anim("AnimFadeInText", "Fade In",
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
            const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
            l.opacity(eased);
        });
}

// ── AnimSlideText: text slides in from right ────────────────────────────
Composition anim_slide_text() {
    return make_basic_anim("AnimSlideText", "Slide In",
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
            const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 slide_x = (1.0f - eased) * 200.0f;
            l.position({slide_x, 0.0f, 0.0f})
             .opacity(eased);
        });
}

// ── AnimScaleText: text scales up from small ────────────────────────────
Composition anim_scale_text() {
    return make_basic_anim("AnimScaleText", "Scale Up",
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
            const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutBack);
            const f32 sv = 0.3f + eased * 0.7f;
            l.opacity(eased)
             .scale({sv, sv, 1.0f});
        });
}

// ── AnimTypewriter: per-character typewriter reveal ─────────────────────
Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        text::typewriter_build(s, "tw", {
            .text = "Typewriter",
            .box = {1200.0f, 240.0f},
            .font_size = 64.0f,
            .tracking = 3.0f,
            .chars_per_frame = 0.3f,
            .easing = EasingCurve{Easing::OutCubic},
        }, ctx.frame);
        return s.build();
    });
}

} // namespace chronon3d::content::anims
