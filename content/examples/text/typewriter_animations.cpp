// ============================================================================
// content/examples/text/typewriter_animations.cpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — 5 typewriter animation factories
// (stable per-glyph reveal).
//
// Extracted from the >360-LoC monolithic `content/examples/text/text_animations.cpp`
// (now deleted; replaced by this cpp + easy_text_animations.cpp +
// text_animation_registration.cpp).
//
// KEY FIX (preserved verbatim from the original): No more substring-based
// layout! Each character is pre-positioned at its final location using
// full-text layout metrics. Only opacity changes per frame. The text block
// stays perfectly stable throughout the animation.
//
// Uses the Step 10 shared helpers from `content/common/text_reveal_helpers.hpp`:
//   - `build_text_reveal_line(s, TextRevealDescriptor{...})` — per-line typewriter
//   - `build_2line_typewriter(s, l1, fs1, l2, fs2, start_delay_2, ...)` — 2-line
//   - `measure_text_width(text, fs, spec, tracking, engine)` — width probe
//   - `font_regular()` — canonical Inter-Regular font spec
//
// Ripgrep verification (per user spec §17): NO local helper duplicates
// `build_2line_typewriter` logic in this file. The 5 factories CALL the
// canonical helpers; the helpers are defined ONCE in text_reveal_helpers.hpp.
// ============================================================================

#include "typewriter_animations.hpp"

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/common/background_helpers.hpp"
#include "content/common/text_reveal_helpers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace chronon3d::content::anims {

using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::animation_helpers::SHADOW_COLOR;
using chronon3d::content::text_reveal::TextRevealDescriptor;
using chronon3d::content::text_reveal::build_2line_typewriter;
using chronon3d::content::text_reveal::build_text_reveal_line;
using chronon3d::content::text_reveal::measure_text_width;
using chronon3d::content::text_reveal::font_regular;

namespace {

using chronon3d::content::animation_helpers::FONT_REGULAR;

constexpr f32 TW_BASE_Y = -50.0f;
constexpr f32 TW_BOX_H = 130.0f;
constexpr f32 TW_TRACKING = 4.0f;

// MinimalistGrid background for the typewriters.
void tw_add_bg(SceneBuilder& s) {
    chronon3d::content::backgrounds::add_common_background(
        s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  5 TYPEWRITER ANIMATIONS — Stable per-glyph reveal
//
//  Each character is pre-positioned at its final location using full-text
//  layout metrics. Only opacity animates per frame.
//  The text block stays perfectly stable throughout.
// ═══════════════════════════════════════════════════════════════════════════

// ── 1. AnimTypewriterSimple — 2-line, chars fade in ─────────────────────
Composition anim_typewriter_simple() {
    return composition({.name = "AnimTypewriterSimple", .width = 1920, .height = 1080, .duration = 100},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());

        // Precompute widths FIRST, then use inline build_text_reveal_line calls
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TW_TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TW_TRACKING, *s.font_engine());
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "THIS TEXT APPEARS", .font_size = 64.0f, .font_spec = spec,
            .tracking = TW_TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, TW_BASE_Y - 42.5f, 0.0f},
            .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
            .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_0"
        });
        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "ONE LETTER AT A TIME", .font_size = 76.0f, .font_spec = spec,
            .tracking = TW_TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, TW_BASE_Y + 42.5f, 0.0f},
            .start_delay = 36.0f, .duration = 8.0f, .stagger = 2.0f,
            .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_1"
        });

        return s.build();
    });
}

// ── 2. AnimTypewriterCursor — 2-line + cursor on line 2 ─────────────────
Composition anim_typewriter_cursor() {
    return composition({.name = "AnimTypewriterCursor", .width = 1920, .height = 1080, .duration = 110},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());

        // Precompute needed values BEFORE build_2line_typewriter
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TW_TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TW_TRACKING, *s.font_engine());
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;
        f32 cursor_x = ref_x + w2 + 6.0f;

        // Use the same build_2line_typewriter as Simple (proven stable)
        build_2line_typewriter(s,
            "THIS TEXT APPEARS",   64.0f,
            "ONE LETTER AT A TIME", 76.0f,
            36.0f);

        // Cursor appears after line 2 fully reveals
        const f32 cursor_delay = 36.0f + 18.0f * 2.0f + 8.0f + 4.0f;

        s.layer("cursor", [cursor_x, cursor_delay](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({cursor_x, TW_BASE_Y + 42.0f, 0.0f});

            // Blink: 6 frames on, 6 frames off, starting after cursor_delay
            auto& op = l.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Hold});
            op.add_keyframe(Frame{static_cast<Frame>(cursor_delay)}, 0.0f, EasingCurve{Easing::Hold});
            op.add_keyframe(Frame{static_cast<Frame>(cursor_delay)}, 1.0f, EasingCurve{Easing::Linear});
            for (f32 t = cursor_delay + 6.0f; t < 120.0f; t += 12.0f) {
                op.add_keyframe(Frame{static_cast<Frame>(t)}, 0.0f, EasingCurve{Easing::Linear});
                op.add_keyframe(Frame{static_cast<Frame>(t + 6.0f)}, 1.0f, EasingCurve{Easing::Linear});
            }

            TextSpec ts;
            ts.content.value = "|";
            ts.layout.box = {20.0f, TW_BOX_H};
            ts.placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}};
            ts.font.font_path = FONT_REGULAR;
            ts.font.font_size = 76.0f;
            ts.appearance.color = TEXT_COLOR;
            ts.layout.align = TextAlign::Center;
            ts.layout.vertical_align = VerticalAlign::Middle;
            ts.layout.line_height = 1.22f;

            l.text("cursor_label", ts);
        });

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

        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TW_TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TW_TRACKING, *s.font_engine());
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "THIS TEXT APPEARS", .font_size = 64.0f, .font_spec = spec,
            .tracking = TW_TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, TW_BASE_Y - 42.5f, 0.0f},
            .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
            .slide_up = true, .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_0"
        });
        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "ONE LETTER AT A TIME", .font_size = 76.0f, .font_spec = spec,
            .tracking = TW_TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, TW_BASE_Y + 42.5f, 0.0f},
            .start_delay = 36.0f, .duration = 8.0f, .stagger = 2.0f,
            .slide_up = true, .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_1"
        });

        return s.build();
    });
}

// ── 4. AnimTypewriterGlow — 2-line typewriter reveal with glow ──────────
//
// Stable per-glyph approach: each character is pre-positioned at its final
// location using full-text layout metrics. Only opacity and glow animate.
// The text block stays perfectly stable throughout the animation.
//
Composition anim_typewriter_glow() {
    return composition({.name = "AnimTypewriterGlow", .width = 1920, .height = 1080, .duration = 160},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());

        // Stable per-glyph typewriter with glow on revealed characters
        build_2line_typewriter(s,
            "THIS TEXT APPEARS",    88.0f,
            "ONE LETTER AT A TIME", 104.0f,
            68.0f,   // start_delay_2 (line 2 starts later)
            120.0f,  // line_spacing (match original line_gap)
            false,   // slide_up
            0.5f);   // glow_intensity — subtle micro glow on each char

        return s.build();
    });
}

// ── 5. AnimTypewriterStagger — 4-line rhythmic stagger ─────────────────
Composition anim_typewriter_stagger() {
    return composition({.name = "AnimTypewriterStagger", .width = 1920, .height = 1080, .duration = 120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        tw_add_bg(s);
        s.font_engine(ctx.font_engine_or_null());

        const struct { const char* text; f32 size; f32 delay; } lines[] = {
            {"THIS TEXT",   60.0f,  0.0f},
            {"APPEARS",     60.0f, 20.0f},
            {"ONE LETTER",  68.0f, 40.0f},
            {"AT A TIME",   60.0f, 60.0f},
        };

        // Find the widest line to use as shared left-edge alignment
        auto spec = font_regular();
        f32 max_w = 0.0f;
        for (int i = 0; i < 4; ++i) {
            f32 w = measure_text_width(lines[i].text, lines[i].size, spec, TW_TRACKING, *s.font_engine());
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
