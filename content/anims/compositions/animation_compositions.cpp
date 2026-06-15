#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"

#include <algorithm>

namespace chronon3d::content::anims {

using namespace chronon3d::content::animation_helpers;

// ── AnimFadeInText: text fades in smoothly ──────────────────────────────
Composition anim_fade_in_text() {
    return composition({.name = "AnimFadeInText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);

        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
        const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);

        s.layer("text", [eased](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .opacity(eased)
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", make_text("Fade In", 64.0f));
        });
        return s.build();
    });
}

// ── AnimSlideText: text slides in from right ────────────────────────────
Composition anim_slide_text() {
    return composition({.name = "AnimSlideText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);

        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
        const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
        const f32 slide_x = (1.0f - eased) * 200.0f;

        s.layer("text", [eased, slide_x](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({slide_x, 0.0f, 0.0f})
             .opacity(eased)
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", make_text("Slide In", 64.0f));
        });
        return s.build();
    });
}

// ── AnimScaleText: text scales up from small ────────────────────────────
Composition anim_scale_text() {
    return composition({.name = "AnimScaleText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);

        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
        const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutBack);
        const f32 sv = 0.3f + eased * 0.7f;

        s.layer("text", [eased, sv](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .opacity(eased)
             .scale({sv, sv, 1.0f})
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", make_text("Scale Up", 64.0f));
        });
        return s.build();
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
