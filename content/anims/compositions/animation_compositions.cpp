#include <chronon3d/core/composition/composition_registry.hpp>
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

// Forward-declare factories from companion files
Composition anim_slide_up();
Composition anim_scale_pop();
Composition anim_blur_focus();
Composition anim_slide_left();
Composition anim_bounce_drop();
Composition anim_typewriter_simple();
Composition anim_typewriter_cursor();
Composition anim_typewriter_slide();
Composition anim_typewriter_glow();
Composition anim_typewriter_stagger();
Composition catmull_rom_showcase();
Composition dolly_zoom_showcase();
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
Composition camera_spline_comparison();
#endif
Composition tilt_sweep_title();
Composition tilt_sweep_title_v2();

// Cinematic text + camera compositions (post-diet registration path).
// Defined in cinematic_text_camera.cpp (same translation-unit group).
Composition deep_parallax_cascade();
Composition whip_pan_hero_reveal();
Composition orbit_handheld_glow();
Composition rack_focus_title_swap();
Composition abyss_freefall_stagger();

// ── Per-domain registration ──────────────────────────────────────────────────
void register_anim_compositions(CompositionRegistry& registry) {
    static bool done = false;
    if (done) return;
    done = true;
    registry.add("AnimFadeInText", [](const CompositionProps&) { return anim_fade_in_text(); });
    registry.add("AnimSlideText", [](const CompositionProps&) { return anim_slide_text(); });
    registry.add("AnimScaleText", [](const CompositionProps&) { return anim_scale_text(); });
    registry.add("AnimTypewriter", [](const CompositionProps&) { return anim_typewriter(); });
    registry.add("AnimSlideUp", [](const CompositionProps&) { return anim_slide_up(); });
    registry.add("AnimScalePop", [](const CompositionProps&) { return anim_scale_pop(); });
    registry.add("AnimBlurFocus", [](const CompositionProps&) { return anim_blur_focus(); });
    registry.add("AnimSlideLeft", [](const CompositionProps&) { return anim_slide_left(); });
    registry.add("AnimBounceDrop", [](const CompositionProps&) { return anim_bounce_drop(); });
    registry.add("AnimTypewriterSimple", [](const CompositionProps&) { return anim_typewriter_simple(); });
    registry.add("AnimTypewriterCursor", [](const CompositionProps&) { return anim_typewriter_cursor(); });
    registry.add("AnimTypewriterSlide", [](const CompositionProps&) { return anim_typewriter_slide(); });
    registry.add("AnimTypewriterGlow", [](const CompositionProps&) { return anim_typewriter_glow(); });
    registry.add("AnimTypewriterStagger", [](const CompositionProps&) { return anim_typewriter_stagger(); });
    registry.add("CatmullRomShowcase", [](const CompositionProps&) { return catmull_rom_showcase(); });
    registry.add("DollyZoomShowcase", [](const CompositionProps&) { return dolly_zoom_showcase(); });
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    registry.add("CameraSplineComparison", [](const CompositionProps&) { return camera_spline_comparison(); });
#endif
    registry.add("TiltSweepTitle", [](const CompositionProps&) { return tilt_sweep_title(); });
    registry.add("TiltSweepTitleV2", [](const CompositionProps&) { return tilt_sweep_title_v2(); });

    // Cinematic text + camera compositions (5 new, see cinematic_text_camera.cpp).
    registry.add("DeepParallaxCascade", [](const CompositionProps&) { return deep_parallax_cascade(); });
    registry.add("WhipPanHeroReveal", [](const CompositionProps&) { return whip_pan_hero_reveal(); });
    registry.add("OrbitHandheldGlow", [](const CompositionProps&) { return orbit_handheld_glow(); });
    registry.add("RackFocusTitleSwap", [](const CompositionProps&) { return rack_focus_title_swap(); });
    registry.add("AbyssFreefallStagger", [](const CompositionProps&) { return abyss_freefall_stagger(); });
}

} // namespace chronon3d::content::anims
