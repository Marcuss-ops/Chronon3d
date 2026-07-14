#pragma once

// ── text_reveal — per-glyph animation reveal (descriptor + builder) ──────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 3 of 4).  Single-responsibility: animation reveal — the
// TextRevealDescriptor struct + the build_text_reveal_line() function
// that materialises one layer per character at its final pre-computed
// position with per-frame opacity / slide-up / glow animation.
//
// DEPENDENCIES: glyph_layout (for layout_glyphs()).
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 12 existing callers' `using`
// declarations).

#include <chronon3d/animation/easing/easing.hpp>   // Easing, EasingCurve
#include <chronon3d/core/types/types.hpp>          // Frame, f32, Vec2, Vec3, Color (canonical SDK types header)
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/font_engine.hpp>  // FontSpec

#include <string>

namespace chronon3d::content::text_reveal {

// P0-2 forward declaration: 3-arg overload of build_text_reveal_line below
// takes a `const ShapedGlyphLine&` parameter.  ShapedGlyphLine is fully
// defined in content/common/text/glyph_layout.hpp; we forward-declare it
// here to keep this header from pulling glyph_layout.hpp into every TU that
// only needs the 2-arg overload, while still allowing the 3-arg declaration
// to compile (const-ref parameter types require only a forward declaration).
class ShapedGlyphLine;

// TextRevealDescriptor — per-glyph reveal parameters
//
// Replaces both build_stagger_line (cinematic_text_camera.cpp) and
// build_typewriter_line (text_animations.cpp).  All parameters live in the
// descriptor struct; callers that need different behaviours set different
// fields rather than passing a long positional argument list.
struct TextRevealDescriptor {
    // ── Text content ──
    std::string text;
    f32         font_size{72.0f};
    FontSpec    font_spec;          // must set .font_path (REQUIRED)

    // ── Layout ──
    f32 tracking{4.0f};
    f32 ref_offset_x{0.0f};        // set to -max_width/2 for center alignment

    // ── Position ──
    Vec3 base_pos{0.0f, 0.0f, 0.0f};  // y-pos of the text line

    // ── Timing ──
    f32 start_delay{0.0f};         // frames before first char starts
    f32 duration{8.0f};            // frames per char to reach final state
    f32 stagger{2.0f};             // frames between consecutive chars

    // ── Reveal style ──
    bool slide_up{false};           // slide from below during reveal
    f32  slide_up_px{30.0f};       // pixels to slide (used when slide_up=true)
    EasingCurve opacity_easing{Easing::OutCubic};
    EasingCurve position_easing{Easing::OutCubic};  // only used for slide_up

    // ── Canvas pinning ──
    bool  pin_to_center{false};  // call l.pin_to(Anchor::Center) before positioning

    // ── Appearance ──
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    bool  add_shadow{true};
    Vec2  shadow_offset{0.0f, 3.0f};
    Color shadow_color{0.0f, 0.0f, 0.0f, 0.15f};
    f32   shadow_blur{6.0f};

    // ── Glow (optional) ──
    f32  glow_intensity{0.0f};     // 0 = no glow; >0 adds per-char glow
    Color glow_color{0.87f, 0.92f, 1.0f, 1.0f};

    // ── Layer naming ──
    std::string layer_prefix{"ch"};  // prefix for layer names
};

// build_text_reveal_line — creates one layer per character at its final
// pre-computed position.  Only opacity (and optionally position) animate
// per frame — the text block stays perfectly stable.  Throws
// std::runtime_error if SceneBuilder has no FontEngine (fail-loud per
// AGENTS.md §honesty).
//
// Single-call replacement for:
//   cinematic_text_camera.cpp: build_stagger_line(...)
//   text_animations.cpp:       build_typewriter_line(...)
void build_text_reveal_line(SceneBuilder& s, const TextRevealDescriptor& d);

// build_text_reveal_line (P0-2 perf overload) — same effect as the 2-arg
// form, but with the ShapedGlyphLine pre-shaped by the caller.  The
// pre_shaped instance MUST have been constructed with ref_offset_x=0.0f
// (the canonical raw-shape pattern: shape_glyph_line() and
// ShapedGlyphLine::try_shape() both default to a zero ref-offset).  The
// reveal emitter adds d.ref_offset_x to each glyph center at layer-build
// time, preserving byte-equivalence with the 2-arg path.  No engine
// check is required: pre_shaped was already produced via a successful
// engine.shape_text call.
//
// Use this overload from sites that already shape the same text once for
// layout/width computation (in particular build_2line_typewriter, which
// shapes both lines to derive a center-aligned ref_x).  Avoids re-shaping
// the same text twice (4 shape calls → 2 per build_2line_typewriter
// invocation).
void build_text_reveal_line(SceneBuilder& s, const TextRevealDescriptor& d, const ShapedGlyphLine& pre_shaped);

// font_bold — quick factory for the production Poppins-Bold FontSpec
// (lives in text_reveal because callers that build reveal text use it as
// the default font; relocating to glyph_layout would not match the
// canonical call-site pattern).
FontSpec font_bold();

// font_regular — quick factory for the production Poppins-Regular FontSpec.
FontSpec font_regular();

} // namespace chronon3d::content::text_reveal
