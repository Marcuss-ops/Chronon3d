#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
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
// Dead code (removed): make_basic_anim and BasicAnimSetup — replaced by
// the unified make_text_anim in content/common/animation_helpers.hpp.

// ── AnimFadeInText: text fades in smoothly ──────────────────────────────
Composition anim_fade_in_text() {
    return make_text_anim("AnimFadeInText", make_text("Fade In", 64.0f),
                          Frame{60}, TextAnimBg::Black,
        [](LayerBuilder& l) {
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// ── AnimSlideText: text slides in from right ────────────────────────────
Composition anim_slide_text() {
    return make_text_anim("AnimSlideText", make_text("Slide In", 64.0f),
                          Frame{60}, TextAnimBg::Black,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{200.0f, 0.0f, 0.0f})
                .to(Frame{6}, Vec3{0.0f, 0.0f, 0.0f}, Easing::OutCubic)
                .apply_to(l.position_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// ── AnimScaleText: text scales up from small ────────────────────────────
Composition anim_scale_text() {
    return make_text_anim("AnimScaleText", make_text("Scale Up", 64.0f),
                          Frame{60}, TextAnimBg::Black,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.3f, 0.3f, 1.0f})
                .to(Frame{6}, Vec3{1.0f, 1.0f, 1.0f}, Easing::OutBack)
                .apply_to(l.scale_anim());
            text_anim_opacity_outback().apply_to(l.opacity_anim());
        });
}

// ── AnimTypewriter: per-character typewriter reveal ─────────────────────
// F0.2b — static current_path() resolver REMOVED.  FontEngine is now
// supplied via ctx.font_engine (wired by the runtime chain:
// RenderEngine::set_assets_root → Runtime::resolver() → FontEngine).
// F0.3 — typewriter_build now returns Result<bool, TextError>;
// structured error replaces silent return.
Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        if (ctx.font_engine) {
            auto result = text::typewriter_build(s, "tw", {
                .text = "Typewriter",
                .box = {1200.0f, 240.0f},
                .font_size = 64.0f,
                .tracking = 3.0f,
                .chars_per_frame = 0.3f,
                .easing = EasingCurve{Easing::OutCubic},
            }, ctx.frame, *ctx.font_engine);
            // F0.3 — structured error: log and continue (best-effort render).
            // Non-fatal — the scene still builds with other layers intact.
            // TODO: wire spdlog or telemetry when content/ gains logging.
            if (!result) {
                // silent degrade: same behaviour as the previous void-return path
            }
        }
        return s.build();
    });
}

} // anonymous namespace

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

// AE parity stress test — 360-frame multi-segment composition that
// compares Chronon3D's camera stack against After Effects Classic 3D.
// Defined in ae_camera_text_parity.cpp (experimental diagnostics).
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
Composition ae_camera_text_parity();
#endif

// ── Per-domain registration ──────────────────────────────────────────────────
void register_anim_compositions(CompositionRegistry& registry) {
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

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    // AE parity stress test (see ae_camera_text_parity.cpp).  360 frames
    // covering static / dolly-zoom / orbit / rack-focus / whip-pan+
    // motion-blur / stress — renders cleanly via the standard CLI.
    registry.add("AECameraTextParity", [](const CompositionProps&) { return ae_camera_text_parity(); });
#endif
}

} // namespace chronon3d::content::anims
