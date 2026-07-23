#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/motion.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"

#include <spdlog/spdlog.h>   // P0 #3 — eliminate silent failure in AnimTypewriter
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — text animation registration entry
// point.  The 5 easy + 5 typewriter composition registrations live in
// `content/examples/text/text_animation_registration.cpp`.
#include "content/examples/text/text_animation_registration.hpp"

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
            chronon3d::timeline(Vec3{200.0f, 0.0f, 0.0f})
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
            chronon3d::timeline(Vec3{0.3f, 0.3f, 1.0f})
                .to(Frame{6}, Vec3{1.0f, 1.0f, 1.0f}, Easing::OutBack)
                .apply_to(l.scale_anim());
            text_anim_opacity_outback().apply_to(l.opacity_anim());
        });
}

// ── AnimTypewriter: per-character typewriter reveal ─────────────────────
// F0.2b — static current_path() resolver REMOVED.
// F0.3 — typewriter_build now returns Result<bool, TextError>;
//        structured error replaces silent return (non-fatal best-effort).
// WP-9 PR 9.0 — anim_typewriter now sources the engine via
//        `ctx.runtime->font_engine()` (the SOLE canonical path; P1-16 migration);
//        see `docs/adr/ADR-020-shared-static-fontengine-singleton.md`.
Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        if (FontEngine* engine = (ctx.runtime ? &ctx.runtime->font_engine() : nullptr)) {
            auto result = text::typewriter_build(s, "tw", {
                .text = "Typewriter",
                .box = {1200.0f, 240.0f},
                .font_size = 64.0f,
                .tracking = 3.0f,
                .chars_per_frame = 0.3f,
                .easing = EasingCurve{Easing::OutCubic},
            }, ctx.frame(), *engine);
            // P0 #3 — structured error logged via spdlog (frame + message).
            // Non-fatal (best-effort): other layers of the scene still render.
            // Propagating the error up to the render job when text is REQUIRED
            // is a deferred followup (see docs/FOLLOWUP_TICKETS.md — no
            // dedicated ticket yet; tracked as F0.4 in this file's history).
            // NB: result.error().code (TextErrorCode) is intentionally NOT
            // logged here — the enum has no fmt::formatter<> specialization,
            // which would trip fmt::v12::type_is_unformattable_for.
            if (!result) {
                spdlog::error(
                    "[AnimTypewriter] typewriter_build failed: frame={} message=\"{}\"",
                    ctx.frame().integral(),
                    result.error().message);
            }
        }
        return s.build();
    });
}

} // anonymous namespace

// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — forward declarations for the 10
// text animation compositions have been moved to:
//   - content/examples/text/easy_text_animations.hpp (5 easy anims)
//   - content/examples/text/typewriter_animations.hpp (5 typewriters)
// The monolithic `content/examples/text/text_animations.hpp` is DELETED.

// Forward-declare factories from companion files
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
    registry.add(CompositionDescriptor{
        .id = "AnimFadeInText",
        .factory = [](const CompositionProps&) { return anim_fade_in_text(); }});
    registry.add(CompositionDescriptor{
        .id = "AnimSlideText",
        .factory = [](const CompositionProps&) { return anim_slide_text(); }});
    registry.add(CompositionDescriptor{
        .id = "AnimScaleText",
        .factory = [](const CompositionProps&) { return anim_scale_text(); }});
    registry.add(CompositionDescriptor{
        .id = "AnimTypewriter",
        .factory = [](const CompositionProps&) { return anim_typewriter(); }});

    // TICKET-REFACTOR-CONTENT-EXAMPLES-17 — the 10 text animation registrations
    // (5 easy + 5 typewriters) have been moved to
    // `content/examples/text/text_animation_registration.cpp`.  This call
    // replaces the 10 inline `registry.add(...)` lines that lived here.
    // (See TICKET-REFACTOR-CONTENT-EXAMPLES-17 §A+§B for the full split.)
    register_text_animation_compositions(registry);

    registry.add(CompositionDescriptor{
        .id = "CatmullRomShowcase",
        .factory = [](const CompositionProps&) { return catmull_rom_showcase(); }});
    registry.add(CompositionDescriptor{
        .id = "DollyZoomShowcase",
        .factory = [](const CompositionProps&) { return dolly_zoom_showcase(); }});
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    registry.add(CompositionDescriptor{
        .id = "CameraSplineComparison",
        .factory = [](const CompositionProps&) { return camera_spline_comparison(); }});
#endif
    registry.add(CompositionDescriptor{
        .id = "TiltSweepTitle",
        .factory = [](const CompositionProps&) { return tilt_sweep_title(); }});
    registry.add(CompositionDescriptor{
        .id = "TiltSweepTitleV2",
        .factory = [](const CompositionProps&) { return tilt_sweep_title_v2(); }});

    // Cinematic text + camera compositions (5 new, see cinematic_text_camera.cpp).
    registry.add(CompositionDescriptor{
        .id = "DeepParallaxCascade",
        .factory = [](const CompositionProps&) { return deep_parallax_cascade(); }});
    registry.add(CompositionDescriptor{
        .id = "WhipPanHeroReveal",
        .factory = [](const CompositionProps&) { return whip_pan_hero_reveal(); }});
    registry.add(CompositionDescriptor{
        .id = "OrbitHandheldGlow",
        .factory = [](const CompositionProps&) { return orbit_handheld_glow(); }});
    registry.add(CompositionDescriptor{
        .id = "RackFocusTitleSwap",
        .factory = [](const CompositionProps&) { return rack_focus_title_swap(); }});
    registry.add(CompositionDescriptor{
        .id = "AbyssFreefallStagger",
        .factory = [](const CompositionProps&) { return abyss_freefall_stagger(); }});

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    // AE parity stress test (see ae_camera_text_parity.cpp).  360 frames
    // covering static / dolly-zoom / orbit / rack-focus / whip-pan+
    // motion-blur / stress — renders cleanly via the standard CLI.
    registry.add(CompositionDescriptor{
        .id = "AECameraTextParity",
        .factory = [](const CompositionProps&) { return ae_camera_text_parity(); }});
#endif
}

} // namespace chronon3d::content::anims
