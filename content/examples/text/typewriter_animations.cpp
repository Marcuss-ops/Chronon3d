// ============================================================================
// content/examples/text/typewriter_animations.cpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — 5 typewriter animation factories
// (stable per-glyph reveal).
//
// refactor(typewriter): TwoLineTypewriterSpec drives Anim* (Point 11) —
// AnimTypewriterSimple/Slide/Glow reduced from ~30-50 LoC to ~10-15 LoC
// each via TwoLineTypewriterSpec struct (cat-3 minimal-surface).  The
// hardcoded cursor_delay (36.0f + 18.0f * 2.0f + 8.0f + 4.0f = 84) on
// AnimTypewriterCursor was replaced with canvas-derived math:
//   `block.second_line_reveal_end + 6.0f`
// where second_line_reveal_end is computed canonically by
// build_2line_typewriter (start_delay + (n-1)*stagger + duration = 78
// for "ONE LETTER AT A TIME").  78 + 6 = 84 byte-equivalent to the
// pre-refactor cursor_delay.
//
// KEY FIX (preserved verbatim from the original): No more substring-based
// layout! Each character is pre-positioned at its final location using
// full-text layout metrics. Only opacity changes per frame. The text block
// stays perfectly stable throughout the animation.
// ============================================================================

#include "typewriter_animations.hpp"

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/common/background_helpers.hpp"
// Phase B (text_reveal_helpers split): granular includes —
//   (a) text_reveal for TextRevealDescriptor (Stagger factory only — kept
//       for the 4-line case which is out-of-scope for Point 11)
//   (b) typewriter_block for TwoLineTypewriterSpec + build_2line_typewriter
//   (c) glyph_layout NOT included here: shape_glyph_line is used internally
//       in typewriter_block, never directly (Point 8 clean separation).
#include "content/common/text/text_reveal.hpp"
#include "content/common/text/typewriter_block.hpp"

#include <string>
#include <vector>

namespace chronon3d::content::anims {

using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::animation_helpers::SHADOW_COLOR;
using chronon3d::content::animation_helpers::FONT_REGULAR;
using chronon3d::content::text_reveal::build_2line_typewriter;
using chronon3d::content::text_reveal::build_text_reveal_line;
using chronon3d::content::text_reveal::font_regular;
using chronon3d::content::text_reveal::measure_text_width;
using chronon3d::content::text_reveal::TextRevealDescriptor;

namespace {

constexpr f32 TW_BASE_Y = -50.0f;
constexpr f32 TW_BOX_H  = 130.0f;
constexpr f32 TW_CURSOR_Y = TW_BASE_Y + 42.0f;        // matches pre-Point-11 cursor y
constexpr f32 TW_CURSOR_GAP = 6.0f;                   // distance from line 2 right edge
constexpr f32 TW_CURSOR_POST_REVEAL_BUFFER = 6.0f;    // matches pre-Point-11 visual pause 78+6=84
constexpr f32 TW_TRACKING = 4.0f;                     // Poppins-Regular 4-px tracking

// MinimalistGrid background for the typewriters.
void tw_add_bg(SceneBuilder& s) {
    chronon3d::content::backgrounds::add_common_background(
        s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
}

// add_cursor — blinking text cursor at (right_edge + gap, y) that appears
// after `reveal_end + TW_CURSOR_POST_REVEAL_BUFFER` (= 84 for "ONE LETTER
// AT A TIME" line 2 — byte-equivalent with pre-Point-11 cursor_delay).
//
// Point 11: extracted from inline `s.layer("cursor", ...)` block inside
// AnimTypewriterCursor.  Caller passes the canonical pre-computed
// block.second_line_right_edge + block.second_line_reveal_end instead of
// recomputing widths + character counts + hardcoded delays.
//
// Internal anon-namespace helper (Cat-3 minimal-surface: single caller
// does NOT justify promoting to typewriter_block.hpp public API).
void add_cursor(SceneBuilder& s, f32 right_edge, Frame reveal_end,
                f32 size) {
    const f32 cursor_x = right_edge + TW_CURSOR_GAP;
    const Frame cursor_appear{static_cast<Frame>(
        static_cast<f32>(reveal_end.value) + TW_CURSOR_POST_REVEAL_BUFFER)};

    s.layer("cursor", [cursor_x, cursor_appear, size](LayerBuilder& l) {
        l.pin_to(Anchor::Center)
         .position({cursor_x, TW_CURSOR_Y, 0.0f});

        // Blink: 6 frames on, 6 frames off, starting at cursor_appear.
        auto& op = l.opacity_anim();
        op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Hold});
        op.add_keyframe(cursor_appear, 0.0f, EasingCurve{Easing::Hold});
        op.add_keyframe(cursor_appear, 1.0f, EasingCurve{Easing::Linear});
        for (f32 t = static_cast<f32>(cursor_appear.value) + 6.0f;
             t < 120.0f; t += 12.0f) {
            op.add_keyframe(Frame{static_cast<Frame>(t)},         0.0f, EasingCurve{Easing::Linear});
            op.add_keyframe(Frame{static_cast<Frame>(t + 6.0f)}, 1.0f, EasingCurve{Easing::Linear});
        }

        TextSpec ts;
        ts.content.value = "|";
        ts.layout.box = {20.0f, TW_BOX_H};
        ts.placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}};
        ts.font.font_path = FONT_REGULAR;
        ts.font.font_size = size;
        ts.appearance.color = TEXT_COLOR;
        ts.layout.align = TextAlign::Center;
        ts.layout.vertical_align = VerticalAlign::Middle;
        ts.layout.line_height = 1.22f;

        l.text("cursor_label", ts);
    });
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 5 TYPEWRITER ANIMATIONS — Stable per-glyph reveal
// ═══════════════════════════════════════════════════════════════════════════

// ── 1. AnimTypewriterSimple — 2-line, chars fade in ─────────────────────
Composition anim_typewriter_simple() {
    return composition({.name = "AnimTypewriterSimple", .width = 1920, .height = 1080, .duration = 100},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());
        build_2line_typewriter(s, {
            .first  = {.text = "THIS TEXT APPEARS",    .font_size = 64.0f},
            .second = {.text = "ONE LETTER AT A TIME", .font_size = 76.0f}
        });
        return s.build();
    });
}

// ── 2. AnimTypewriterCursor — 2-line + cursor on line 2 ─────────────────
//
// Point 11: cursor_delay was hardcoded `36.0f + 18.0f * 2.0f + 8.0f + 4.0f`
// (= 84).  Replaced with `block.second_line_reveal_end + 6` where
// second_line_reveal_end = start_delay + (n-1)*stagger + duration
// (= 78 for "ONE LETTER AT A TIME").  78 + 6 = 84 byte-equivalent.
Composition anim_typewriter_cursor() {
    return composition({.name = "AnimTypewriterCursor", .width = 1920, .height = 1080, .duration = 110},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());
        auto block = build_2line_typewriter(s, {
            .first  = {.text = "THIS TEXT APPEARS",    .font_size = 64.0f},
            .second = {.text = "ONE LETTER AT A TIME", .font_size = 76.0f}
        });
        add_cursor(s, block.second_line_right_edge, block.second_line_reveal_end, /*size=*/76.0f);
        return s.build();
    });
}

// ── 3. AnimTypewriterSlide — 2-line, chars slide up ─────────────────────
Composition anim_typewriter_slide() {
    return composition({.name = "AnimTypewriterSlide", .width = 1920, .height = 1080, .duration = 100},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());
        build_2line_typewriter(s, {
            .first  = {.text = "THIS TEXT APPEARS",    .font_size = 64.0f},
            .second = {.text = "ONE LETTER AT A TIME", .font_size = 76.0f},
            .slide_up = true
        });
        return s.build();
    });
}

// ── 4. AnimTypewriterGlow — 2-line typewriter reveal with glow ──────────
Composition anim_typewriter_glow() {
    return composition({.name = "AnimTypewriterGlow", .width = 1920, .height = 1080, .duration = 160},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());
        build_2line_typewriter(s, {
            .first  = {.text = "THIS TEXT APPEARS",    .font_size = 88.0f},
            .second = {.text = "ONE LETTER AT A TIME", .font_size = 104.0f},
            .second_delay   = 68.0f,
            .line_spacing   = 120.0f,
            .glow_intensity = 0.5f
        });
        return s.build();
    });
}

// ── 5. AnimTypewriterStagger — 4-line rhythmic stagger ─────────────────
//
// Out-of-scope for Point 11 (uses TextRevealDescriptor directly because the
// 4 lines have different sizes; not the 2-line helper).  Kept verbatim
// from pre-Point-11.
Composition anim_typewriter_stagger() {
    return composition({.name = "AnimTypewriterStagger", .width = 1920, .height = 1080, .duration = 120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());

        const struct { const char* text; f32 size; f32 delay; } lines[] = {
            {"THIS TEXT",  60.0f,  0.0f},
            {"APPEARS",    60.0f, 20.0f},
            {"ONE LETTER", 68.0f, 40.0f},
            {"AT A TIME",  60.0f, 60.0f},
        };

        auto spec = chronon3d::content::text_reveal::font_regular();
        f32 max_w = 0.0f;
        for (int i = 0; i < 4; ++i) {
            f32 w = measure_text_width(lines[i].text, lines[i].size, spec,
                                       /*tracking=*/TW_TRACKING, *s.font_engine());
            if (w > max_w) max_w = w;
        }
        f32 ref_x = -max_w * 0.5f;

        const f32 start_y = TW_BASE_Y - 108.0f;
        const f32 step_y = 72.0f;

        for (int i = 0; i < 4; ++i) {
            build_text_reveal_line(s, TextRevealDescriptor{
                .text = lines[i].text, .font_size = lines[i].size, .font_spec = spec,
                .tracking = TW_TRACKING, .ref_offset_x = ref_x,
                .base_pos = {0.0f, start_y + static_cast<f32>(i) * step_y, 0.0f},
                .start_delay = lines[i].delay, .duration = 8.0f, .stagger = 3.0f,
                .pin_to_center = true,
                .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
                .layer_prefix = "ch_" + std::to_string(i)
            });
        }

        return s.build();
    });
}

} // namespace chronon3d::content::anims
