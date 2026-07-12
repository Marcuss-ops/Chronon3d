// ── Text Placement Test Suite — Composition Factories ──────────────────────
//
// Canonical test compositions exercising the TextRun placement pipeline.
// Every composition obeys the rule:
//     l.pin_to(Anchor::Center) + from_text_spec(TextSpec{...})  —  NO .pos workaround.
//
// Groups:
//   A. Dashboard (8)  — static, animated, scale, glow, multiline, overflow,
//                        multisource, portrait
//   B. Anti-double     — static vs animated centering parity
//   C. Layout box      — visible debug box + centered text overlay
//   D. Clipping        — blur 0/7/20, glow 40, shadow 80, scale 1.30/2.00
//   E. Multi-resolution — 1920×1080, 1280×720, 1080×1920, 3840×2160
//   F. Cache invalidation — frame-dependent text content
//
// Register via register_text_placement_compositions().
//
// M1.8 §2D / TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS (2026-07-10):
//   - 4 helper functions (make_centered_text_comp, make_1080p_centered,
//     make_clipping_comp, make_multires) refactored to compute the
//     canonical TextDefinition from CenterTextOptions internally;
//     the 9 call sites passing CenterTextOptions unchanged.
//   - 3 inline `centered_text({...})` call sites (A.7 multisource,
//     C.1 box alignment, F.1 cache invalidation) replaced with
//     `from_text_spec(TextSpec{...})` directly.
//   - The legacy `centered_text()` wrapper is no longer invoked
//     anywhere in this file (gate [2/4] satisfied).
//   - `CenterTextOptions` struct definition is preserved (still used
//     as the helper input parameter type — matches the §2D migration
//     principle "minimize breakage of helper signatures").
//   - `#include <content/text/text_helpers.hpp>` is preserved
//     (provides `CenterTextOptions`); the `centered_text`/`glow_text`
//     deprecated wrappers are NOT invoked.

#include "text_placement_compositions.hpp"

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <content/text/text_helpers.hpp>

#include <string>

namespace chronon3d::content::text_placement {

using namespace chronon3d::content::text;   // for CenterTextOptions (input type)

// ═══════════════════════════════════════════════════════════════════════════
// Shared helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

constexpr Color  kDarkBg{0.05f, 0.05f, 0.08f, 1.0f};
constexpr Color  kWhite{1.0f, 1.0f, 1.0f, 1.0f};
constexpr f32    kDefaultFontSize = 96.0f;
constexpr Frame  kDefaultDuration{60};

/// Dark background layer used by all text-placement compositions.
inline void add_dark_background(SceneBuilder& s) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", kDarkBg);
    });
}

/// Standard centered-text layer with pin_to(Anchor::Center).
/// The caller supplies a setup lambda for per-composition customisation
/// (anim, glow, shadow, scale, etc.).
using TextLayerSetup = std::function<void(LayerBuilder&)>;

/// M1.8 §2D — convert the legacy CenterTextOptions (helper input type)
/// to the canonical TextDefinition (F2.A DTO) used by LayerBuilder::text().
/// This is the same conversion that the deprecated `centered_text()`
/// wrapper performed; we inline it here to avoid invoking the deprecated
/// path.  Field mapping is byte-equivalent to the centered_text() body
/// in `content/text/text_helpers_centered.hpp`.
inline TextDefinition options_to_definition(const CenterTextOptions& opts) {
    return from_text_spec(TextSpec{.content    = {.value = opts.text},.position   = opts.pos,.font       = {.font_path   = opts.font_asset,
                       .font_family = opts.font_family,
                       .font_weight = opts.font_weight,
                       .font_style  = opts.font_style,
                       .font_size   = opts.font_size},.layout     = {.box            = opts.box,
                       .anchor         = TextAnchor::Center,
                       .centering_mode = TextCenteringMode::PixelInk,
                       .align          = TextAlign::Center,
                       .vertical_align = VerticalAlign::Middle,
                       .wrap           = TextWrap::Word,
                       .overflow       = TextOverflow::Clip,
                       .line_height    = opts.line_height,
                       .tracking       = opts.tracking,
                       .auto_fit       = opts.auto_fit,
                       .min_font_size  = opts.min_font_size,
                       .max_font_size  = opts.max_font_size,
                       .max_lines      = opts.max_lines},.appearance = {.color = opts.color},});
}

/// Build a composition with dark bg + one centered text layer.
/// All compositions use this skeleton to guarantee the "no .pos workaround"
/// invariant is uniformly applied.
Composition make_centered_text_comp(
    const char* name,
    int width, int height,
    Frame duration,
    CenterTextOptions opts,
    TextLayerSetup setup = {})
{
    // M1.8 §2D — pre-compute the canonical TextDefinition once per
    // composition (NOT per frame; the same def is reused across all
    // frames because the opts are captured by value).  This avoids
    // invoking the deprecated `centered_text()` wrapper at the call
    // site while preserving byte-equivalent field semantics.
    TextDefinition def = options_to_definition(opts);
    return composition(
        {.name = name, .width = width, .height = height, .duration = duration},
        [def = std::move(def), setup = std::move(setup)](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_dark_background(s);
            s.layer("text", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                if (setup) setup(l);
                l.text("label", def);
            });
            return s.build();
        });
}

/// Shortcut: centered text at 1920×1080, 60f duration, default font size.
Composition make_1080p_centered(const char* name, CenterTextOptions opts,
                                 TextLayerSetup setup = {}) {
    return make_centered_text_comp(name, 1920, 1080, kDefaultDuration,
                                    std::move(opts), std::move(setup));
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Group A — Dashboard (8 compositions)
// ═══════════════════════════════════════════════════════════════════════════

// A.1 — Static text, centered, no .pos, no animation.
Composition make_static_center_no_pos() {
    return make_1080p_centered("TextPlaceStaticCenter", {
        .text = "CENTERED",
        .font_size = kDefaultFontSize,
        .tracking = 4.0f,
    });
}

// A.2 — Animated text (fade-in + slight slide-up), centered, no .pos.
Composition make_animated_center_no_pos() {
    return make_1080p_centered("TextPlaceAnimatedCenter", {
        .text = "ANIMATED",
        .font_size = kDefaultFontSize,
        .tracking = 4.0f,
    }, [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, 40.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f},  EasingCurve{Easing::OutCubic});
    });
}

// A.3 — Scale 1.30, centered, no .pos.
Composition make_scale_130_center_no_pos() {
    return make_1080p_centered("TextPlaceScale130", {
        .text = "SCALE 1.30",
        .font_size = kDefaultFontSize,
        .tracking = 3.0f,
    }, [](LayerBuilder& l) {
        l.scale_anim().key(Frame{0}, Vec3{1.30f, 1.30f, 1.0f}, EasingCurve{Easing::Linear});
    });
}

// A.4 — Glow + drop shadow, centered, no .pos.
Composition make_glow_shadow_center_no_pos() {
    return make_1080p_centered("TextPlaceGlowShadow", {
        .text = "GLOW+SHADOW",
        .font_size = kDefaultFontSize,
        .tracking = 5.0f,
    }, [](LayerBuilder& l) {
        l.glow({
            .enabled = true,
            .radius = 24.0f,
            .intensity = 0.50f,
            .color = {0.60f, 0.75f, 1.0f, 1.0f},
            .softness = 1.0f,
            .falloff = 0.80f,
            .core_strength = 0.10f,
            .aura_strength = 0.30f,
            .bloom_strength = 0.50f,
        });
        l.drop_shadow(Vec2{4.0f, 6.0f}, Color{0.0f, 0.0f, 0.0f, 0.30f}, 12.0f);
    });
}

// A.5 — Multiline text, centered middle, no .pos.
Composition make_multiline_center_middle() {
    return make_1080p_centered("TextPlaceMultiline", {
        .text = "LINE ONE\nLINE TWO\nLINE THREE",
        .box = {1600.0f, 400.0f},
        .font_size = 72.0f,
        .tracking = 2.0f,
        .max_lines = 3,
    });
}

// A.6 — Small box with overflow clip, centered, no .pos.
// The box is intentionally narrow so text would overflow without clip.
Composition make_small_box_overflow_clip() {
    return make_1080p_centered("TextPlaceSmallBox", {
        .text = "OVERFLOW CLIP TEST",
        .box = {300.0f, 100.0f},
        .font_size = 48.0f,
        .tracking = 1.0f,
    });
}

// A.7 — Multisource: text + decorative rectangle in the same layer.
// The layer is pinned to center; text and rect share the coordinate space.
// M1.8 §2D — inline `centered_text({...})` replaced with
// `from_text_spec(TextSpec{...})` directly.  All CenterTextOptions
// defaults (box={1200,240}, pos={0,0,0}, font_asset="assets/fonts/Poppins-Bold.ttf",
// font_family="Poppins", font_weight=700, font_style="normal", color=Color{1,1,1,1},
// max_lines=1, auto_fit=false, line_height=0.95, min_font_size=12, max_font_size=160)
// are preserved exactly.
Composition make_multisource_text_plus_shape() {
    return composition(
        {.name = "TextPlaceMultisource", .width = 1920, .height = 1080,
         .duration = kDefaultDuration},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_dark_background(s);
            s.layer("combo", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                // Decorative underline rect (blue, semi-transparent)
                l.rect("underline", RectParams{
                    .size = {600.0f, 6.0f},
                    .color = {0.30f, 0.60f, 1.0f, 0.70f},
                    .pos = {0.0f, 48.0f, 0.0f},
                });
                // Centered text above the underline
                l.text("title", from_text_spec(TextSpec{.content    = {.value = "MULTISOURCE"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font       = {.font_path   = "assets/fonts/Poppins-Bold.ttf",
                                   .font_family = "Poppins",
                                   .font_weight = 700,
                                   .font_style  = "normal",
                                   .font_size   = kDefaultFontSize},.layout     = {.box            = {1200.0f, 240.0f},
                                   .anchor         = TextAnchor::Center,
                                   .centering_mode = TextCenteringMode::PixelInk,
                                   .align          = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle,
                                   .wrap           = TextWrap::Word,
                                   .overflow       = TextOverflow::Clip,
                                   .line_height    = 0.95f,
                                   .tracking       = 6.0f,
                                   .min_font_size  = 12.0f,
                                   .max_font_size  = 160.0f,
                                   .max_lines      = 1},.appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},}));
            });
            return s.build();
        });
}

// A.8 — Portrait 1080×1920, centered, no .pos.
Composition make_portrait_1080x1920_center() {
    return make_centered_text_comp("TextPlacePortrait", 1080, 1920,
                                    kDefaultDuration, {
        .text = "PORTRAIT",
        .box = {900.0f, 300.0f},
        .font_size = 80.0f,
        .tracking = 8.0f,
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Group B — Anti-double-translation
// ═══════════════════════════════════════════════════════════════════════════

// B.1 — Static: pin_to(Center) + text, no animation.
// Expected: alpha centroid ≈ canvas center {960, 540}.
Composition make_antidouble_static() {
    return make_1080p_centered("TextPlaceAntiDoubleStatic", {
        .text = "STATIC",
        .font_size = kDefaultFontSize,
        .tracking = 6.0f,
    });
}

// B.2 — Animated: pin_to(Center) + position_anim + text.
// Expected: alpha centroid ≈ canvas center + anim offset.
// Bug-catcher: if alpha centroid ≈ canvas center * 2 (e.g. {1920, 1080}),
// the graph builder is applying canvas-center translation twice.
Composition make_antidouble_animated() {
    return make_1080p_centered("TextPlaceAntiDoubleAnimated", {
        .text = "ANIMATED",
        .font_size = kDefaultFontSize,
        .tracking = 6.0f,
    }, [](LayerBuilder& l) {
        // Slight animation — moves from offset to origin.
        // At frame 0, text should be at canvas_center + {30, 20}.
        // At frame 30, text should settle at canvas_center.
        l.position_anim()
            .key(Frame{0},  Vec3{30.0f, 20.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f},  EasingCurve{Easing::OutCubic});
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Group C — Layout box alignment
// ═══════════════════════════════════════════════════════════════════════════

// C.1 — Visible debug rect + centered text in the same box dimensions.
// Verifies that align, vertical_align, anchor and box are applied correctly:
// the text alpha centroid should be near the box center, not the top-left.
// M1.8 §2D — inline `centered_text({...})` replaced with
// `from_text_spec(TextSpec{...})` directly.  All CenterTextOptions
// defaults (pos={0,0,0}, font_asset="assets/fonts/Poppins-Bold.ttf",
// font_family="Poppins", font_weight=700, font_style="normal", color=Color{1,1,1,1},
// max_lines=1, auto_fit=false, line_height=0.95, min_font_size=12, max_font_size=160)
// are preserved exactly.
Composition make_box_alignment() {
    return composition(
        {.name = "TextPlaceBoxAlign", .width = 1920, .height = 1080,
         .duration = kDefaultDuration},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_dark_background(s);
            s.layer("boxed_text", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                // Semi-transparent blue debug box with stroke outline
                l.rect("debug_box", RectParams{
                    .size = {1200.0f, 240.0f},
                    .color = {0.0f, 0.0f, 1.0f, 0.12f},
                });
                // Text inside the same box dimensions
                l.text("label", from_text_spec(TextSpec{.content    = {.value = "CENTER"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font       = {.font_path   = "assets/fonts/Poppins-Bold.ttf",
                                   .font_family = "Poppins",
                                   .font_weight = 700,
                                   .font_style  = "normal",
                                   .font_size   = 90.0f},.layout     = {.box            = {1200.0f, 240.0f},
                                   .anchor         = TextAnchor::Center,
                                   .centering_mode = TextCenteringMode::PixelInk,
                                   .align          = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle,
                                   .wrap           = TextWrap::Word,
                                   .overflow       = TextOverflow::Clip,
                                   .line_height    = 0.95f,
                                   .tracking       = 4.0f,
                                   .min_font_size  = 12.0f,
                                   .max_font_size  = 160.0f,
                                   .max_lines      = 1},.appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},}));
            });
            return s.build();
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// Group D — Clipping (blur / glow / shadow / scale)
// ═══════════════════════════════════════════════════════════════════════════

// Shared helper for clipping-test compositions.
// Each call site provides a setup lambda that applies the effect under test.
Composition make_clipping_comp(const char* name, TextLayerSetup setup) {
    return make_1080p_centered(name, {
        .text = "CLIP TEST",
        .font_size = kDefaultFontSize,
        .tracking = 3.0f,
    }, std::move(setup));
}

Composition make_clip_blur_0() {
    return make_clipping_comp("TextPlaceClipBlur0", [](LayerBuilder& l) {
        l.blur(0.0f);
    });
}
Composition make_clip_blur_7() {
    return make_clipping_comp("TextPlaceClipBlur7", [](LayerBuilder& l) {
        l.blur(7.0f);
    });
}
Composition make_clip_blur_20() {
    return make_clipping_comp("TextPlaceClipBlur20", [](LayerBuilder& l) {
        l.blur(20.0f);
    });
}
Composition make_clip_glow_40() {
    return make_clipping_comp("TextPlaceClipGlow40", [](LayerBuilder& l) {
        l.glow({
            .enabled = true,
            .radius = 40.0f,
            .intensity = 0.70f,
            .color = {1.0f, 0.85f, 0.40f, 1.0f},
            .softness = 1.0f,
            .falloff = 0.75f,
            .core_strength = 0.20f,
            .aura_strength = 0.40f,
            .bloom_strength = 0.60f,
        });
    });
}
Composition make_clip_shadow_80() {
    return make_clipping_comp("TextPlaceClipShadow80", [](LayerBuilder& l) {
        l.drop_shadow(Vec2{8.0f, 8.0f}, Color{0.0f, 0.0f, 0.0f, 0.40f}, 16.0f);
    });
}
Composition make_clip_scale_130() {
    return make_clipping_comp("TextPlaceClipScale130", [](LayerBuilder& l) {
        l.scale_anim().key(Frame{0}, Vec3{1.30f, 1.30f, 1.0f}, EasingCurve{Easing::Linear});
    });
}
Composition make_clip_scale_200() {
    return make_clipping_comp("TextPlaceClipScale200", [](LayerBuilder& l) {
        l.scale_anim().key(Frame{0}, Vec3{2.00f, 2.00f, 1.0f}, EasingCurve{Easing::Linear});
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Group E — Multi-resolution
// ═══════════════════════════════════════════════════════════════════════════

// Shared: same text content, different canvas sizes.
// Verifies that pin_to(Anchor::Center) correctly resolves to width/2, height/2
// dynamically — never hardcoded to 960×540.
Composition make_multires(const char* name, int w, int h) {
    return make_centered_text_comp(name, w, h, kDefaultDuration, {
        .text = "CENTER",
        .box = {static_cast<f32>(w) * 0.80f, static_cast<f32>(h) * 0.25f},
        .font_size = (w >= h) ? 96.0f : 64.0f,
        .tracking = 4.0f,
    });
}

Composition make_multires_1920x1080() {
    return make_multires("TextPlaceMultiRes1920x1080", 1920, 1080);
}
Composition make_multires_1280x720() {
    return make_multires("TextPlaceMultiRes1280x720", 1280, 720);
}
Composition make_multires_1080x1920() {
    return make_multires("TextPlaceMultiRes1080x1920", 1080, 1920);
}
Composition make_multires_3840x2160() {
    return make_multires("TextPlaceMultiRes3840x2160", 3840, 2160);
}

// ═══════════════════════════════════════════════════════════════════════════
// Group F — Cache invalidation
// ═══════════════════════════════════════════════════════════════════════════

// F.1 — Text changes across frames: HELLO → WORLD → CENTER.
// Verifies that:
//   - Text actually changes (not served from stale cache)
//   - BBox changes with content
//   - Center remains correct across all frames
// M1.8 §2D — inline `centered_text({...})` replaced with
// `from_text_spec(TextSpec{...})` directly.  The runtime-determined
// `word` value is captured into the TextSpec::content.value (std::string).
Composition make_cache_invalidation() {
    return composition(
        {.name = "TextPlaceCacheInvalidation", .width = 1920, .height = 1080,
         .duration = 90},
        [](const FrameContext& ctx) {
            // Frame-dependent text content
            const char* word;
            if (ctx.frame <= 30)       word = "HELLO";
            else if (ctx.frame <= 60)  word = "WORLD";
            else                       word = "CENTER";

            SceneBuilder s(ctx);
            add_dark_background(s);
            s.layer("text", [word](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.text("label", from_text_spec(TextSpec{.content    = {.value = word},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font       = {.font_path   = "assets/fonts/Poppins-Bold.ttf",
                                   .font_family = "Poppins",
                                   .font_weight = 700,
                                   .font_style  = "normal",
                                   .font_size   = kDefaultFontSize},.layout     = {.box            = {1200.0f, 240.0f},
                                   .anchor         = TextAnchor::Center,
                                   .centering_mode = TextCenteringMode::PixelInk,
                                   .align          = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle,
                                   .wrap           = TextWrap::Word,
                                   .overflow       = TextOverflow::Clip,
                                   .line_height    = 0.95f,
                                   .tracking       = 8.0f,
                                   .min_font_size  = 12.0f,
                                   .max_font_size  = 160.0f,
                                   .max_lines      = 1},.appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},}));
            });
            return s.build();
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// Registration
// ═══════════════════════════════════════════════════════════════════════════

void register_text_placement_compositions(CompositionRegistry& registry) {
    // Group A — Dashboard
    registry.add("TextPlaceStaticCenter",     [](const CompositionProps&) { return make_static_center_no_pos(); });
    registry.add("TextPlaceAnimatedCenter",   [](const CompositionProps&) { return make_animated_center_no_pos(); });
    registry.add("TextPlaceScale130",         [](const CompositionProps&) { return make_scale_130_center_no_pos(); });
    registry.add("TextPlaceGlowShadow",       [](const CompositionProps&) { return make_glow_shadow_center_no_pos(); });
    registry.add("TextPlaceMultiline",        [](const CompositionProps&) { return make_multiline_center_middle(); });
    registry.add("TextPlaceSmallBox",         [](const CompositionProps&) { return make_small_box_overflow_clip(); });
    registry.add("TextPlaceMultisource",      [](const CompositionProps&) { return make_multisource_text_plus_shape(); });
    registry.add("TextPlacePortrait",         [](const CompositionProps&) { return make_portrait_1080x1920_center(); });

    // Group B — Anti-double-translation
    registry.add("TextPlaceAntiDoubleStatic",   [](const CompositionProps&) { return make_antidouble_static(); });
    registry.add("TextPlaceAntiDoubleAnimated", [](const CompositionProps&) { return make_antidouble_animated(); });

    // Group C — Layout box
    registry.add("TextPlaceBoxAlign",          [](const CompositionProps&) { return make_box_alignment(); });

    // Group D — Clipping
    registry.add("TextPlaceClipBlur0",         [](const CompositionProps&) { return make_clip_blur_0(); });
    registry.add("TextPlaceClipBlur7",         [](const CompositionProps&) { return make_clip_blur_7(); });
    registry.add("TextPlaceClipBlur20",        [](const CompositionProps&) { return make_clip_blur_20(); });
    registry.add("TextPlaceClipGlow40",        [](const CompositionProps&) { return make_clip_glow_40(); });
    registry.add("TextPlaceClipShadow80",      [](const CompositionProps&) { return make_clip_shadow_80(); });
    registry.add("TextPlaceClipScale130",      [](const CompositionProps&) { return make_clip_scale_130(); });
    registry.add("TextPlaceClipScale200",      [](const CompositionProps&) { return make_clip_scale_200(); });

    // Group E — Multi-resolution
    registry.add("TextPlaceMultiRes1920x1080", [](const CompositionProps&) { return make_multires_1920x1080(); });
    registry.add("TextPlaceMultiRes1280x720",  [](const CompositionProps&) { return make_multires_1280x720(); });
    registry.add("TextPlaceMultiRes1080x1920", [](const CompositionProps&) { return make_multires_1080x1920(); });
    registry.add("TextPlaceMultiRes3840x2160", [](const CompositionProps&) { return make_multires_3840x2160(); });

    // Group F — Cache invalidation
    registry.add("TextPlaceCacheInvalidation", [](const CompositionProps&) { return make_cache_invalidation(); });
}

} // namespace chronon3d::content::text_placement
