// content/anims/compositions/text_animations.cpp
//
// Text animation compositions — stable per-glyph typewriter reveal.
//
// KEY FIX: No more substring-based layout! Each character is pre-positioned
// at its final location using full-text layout metrics. Only opacity changes
// per frame. The text block stays perfectly stable throughout the animation.
//
// Uses TextAnimator::split_units() and measure_unit_width() from the engine
// to calculate positions once, then creates one layer per character.
//
// 5 Easy Animations (single text layer):
//   1. AnimSlideUp    — slides up from below with fade
//   2. AnimScalePop   — scales from small with overshoot
//   3. AnimBlurFocus  — blurry → sharp via focus_in
//   4. AnimSlideLeft  — slides in from the left
//   5. AnimBounceDrop — drops from above with bounce
//
// 5 Typewriter Animations (stable per-glyph reveal):
//   6. AnimTypewriterSimple   — 2-line, chars fade in left→right
//   7. AnimTypewriterCursor   — 2-line + blinking cursor
//   8. AnimTypewriterSlide    — 2-line, chars slide up as they appear
//   9. AnimTypewriterGlow     — 2-line, micro glow on new chars
//  10. AnimTypewriterStagger  — 4-line rhythmic stagger
//
// Render:
//   chronon3d_cli video AnimTypewriterSimple -o output/AnimTypewriterSimple.mp4

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/common/background_helpers.hpp"
#include "content/common/text_reveal_helpers.hpp"

#include <functional>
#include <string>
#include <algorithm>
#include <vector>

namespace chronon3d::content::anims {

// ── Typography constants (from shared headers) ──────────────────────────
using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::animation_helpers::SHADOW_COLOR;
using chronon3d::content::animation_helpers::FONT_REGULAR;
using chronon3d::content::text_reveal::TextRevealDescriptor;
using chronon3d::content::text_reveal::build_text_reveal_line;
using chronon3d::content::text_reveal::measure_text_width;
using chronon3d::content::text_reveal::font_regular;

namespace {


constexpr f32 BOX_W = 1400.0f;
constexpr f32 BOX_H = 130.0f;
constexpr f32 BASE_Y = -50.0f;

// ── Helpers ──────────────────────────────────────────────────────────────

void add_bg(SceneBuilder& s) {
    chronon3d::content::backgrounds::add_common_background(
        s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
}

// Centered text params (for easy animations)
TextSpec txt_center(std::string text, f32 font_size = 72.0f) {
    return TextSpec{
        .content = {.value = std::move(text)},
        .font = {.font_path = FONT_REGULAR, .font_size = font_size},
        .layout = {.box = {BOX_W, BOX_H}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .line_height = 1.22f, .tracking = 4.0f},
        .appearance = {.color = TEXT_COLOR},
        .position = {0.0f, 0.0f, 0.0f},
    };
}

// ── make_easy_anim ─────────────────────────────────────────────────────────
// Shared helper for the 5 easy text animation compositions.  Encapsulates the
// common skeleton (grid bg, centered layer, drop shadow, 80pt text) and lets
// each composition supply only its animation-specific setup via a lambda.
//
// Previously each composition duplicated ~15 lines of identical boilerplate;
// now they are 5–10 lines of pure animation logic.
using EasyAnimSetup = std::function<void(LayerBuilder& l, const FrameContext& ctx)>;

Composition make_easy_anim(const char* name, const char* text,
                           f32 duration, EasyAnimSetup setup) {
    return composition(
        {.name = name, .width = 1920, .height = 1080,
         .duration = static_cast<Frame>(duration)},
        [text, setup = std::move(setup)](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_bg(s);
            s.layer("text", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l, ctx);
                l.drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
                 .text("label", txt_center(text, 80.0f));
            });
            return s.build();
        });
}

// ── Tracking constant (kept local for easy-anim TextParams below) ────
constexpr f32 TRACKING = 4.0f;

// Build a 2-line typewriter block — all lines share the SAME left edge
void build_2line_typewriter(SceneBuilder& s,
                            const std::string& line1, f32 size1,
                            const std::string& line2, f32 size2,
                            f32 start_delay_2 = 36.0f,
                            f32 line_spacing = 85.0f,
                            bool slide_up = false,
                            f32 glow_intensity = 0.0f)
{
    auto spec = font_regular();
    f32 w1 = measure_text_width(line1, size1, spec, TRACKING);
    f32 w2 = measure_text_width(line2, size2, spec, TRACKING);
    f32 max_w = std::max(w1, w2);
    f32 ref_x = -max_w * 0.5f;

    auto d1 = TextRevealDescriptor{
        .text = line1, .font_size = size1, .font_spec = spec,
        .tracking = TRACKING, .ref_offset_x = ref_x,
        .base_pos = {0.0f, BASE_Y - line_spacing * 0.5f, 0.0f},
        .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
        .slide_up = slide_up, .pin_to_center = true,
        .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
        .glow_intensity = glow_intensity,
        .layer_prefix = "ch_0"
    };
    build_text_reveal_line(s, d1);

    auto d2 = d1;
    d2.text = line2;
    d2.font_size = size2;
    d2.base_pos = {0.0f, BASE_Y + line_spacing * 0.5f, 0.0f};
    d2.start_delay = start_delay_2;
    d2.layer_prefix = "ch_1";
    build_text_reveal_line(s, d2);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  5 EASY ANIMATIONS — each delegates the common skeleton to make_easy_anim.
// ═══════════════════════════════════════════════════════════════════════════

// 1. AnimSlideUp — slides up from below with fade
Composition anim_slide_up() {
    return make_easy_anim("AnimSlideUp", "Slide Up", 60.0f,
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
            const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
            l.position({0.0f, BASE_Y + (1.0f - eased) * 80.0f, 0.0f})
             .opacity(p);
        });
}

// 2. AnimScalePop — scales from small with overshoot
Composition anim_scale_pop() {
    return make_easy_anim("AnimScalePop", "Scale Pop", 60.0f,
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
            const f32 sv = interpolate(p, 0.0f, 0.30f, 0.6f, 1.0f, Easing::OutBack);
            l.position({0.0f, BASE_Y, 0.0f})
             .opacity(p)
             .scale({sv, sv, 1.0f});
        });
}

// 3. AnimBlurFocus — blurry → sharp via focus_in
Composition anim_blur_focus() {
    return make_easy_anim("AnimBlurFocus", "Blur Focus", 60.0f,
        [](LayerBuilder& l, const FrameContext& /*ctx*/) {
            l.position({0.0f, BASE_Y, 0.0f})
             .opacity(1.0f)
             .focus_in(24.0f, Frame{20});
        });
}

// 4. AnimSlideLeft — slides in from the left
Composition anim_slide_left() {
    return make_easy_anim("AnimSlideLeft", "Slide Left", 60.0f,
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
            const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
            l.position({(1.0f - eased) * -120.0f, BASE_Y, 0.0f})
             .opacity(p);
        });
}

// 5. AnimBounceDrop — drops from above with bounce
Composition anim_bounce_drop() {
    return make_easy_anim("AnimBounceDrop", "Bounce Drop", 80.0f,
        [](LayerBuilder& l, const FrameContext& ctx) {
            const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 2.5f);
            const f32 y_offset = interpolate(p, 0.0f, 0.30f, -230.0f, BASE_Y, Easing::OutBounce);
            l.opacity(std::min(1.0f, p * 2.0f))
             .position({0.0f, y_offset, 0.0f});
        });
}

// ═══════════════════════════════════════════════════════════════════════════
//  5 TYPEWRITER ANIMATIONS — Stable per-glyph reveal
//  
//  Each character is pre-positioned at its final location using full-text
//  layout metrics. Only opacity animates per frame.
//  The text block stays perfectly stable throughout.
// ═══════════════════════════════════════════════════════════════════════════

// ── 6. AnimTypewriterSimple — 2-line, chars fade in ─────────────────────
Composition anim_typewriter_simple() {
    return composition({.name = "AnimTypewriterSimple", .width = 1920, .height = 1080, .duration = 100},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);

        // Precompute widths FIRST, then use inline build_text_reveal_line calls
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TRACKING);
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TRACKING);
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "THIS TEXT APPEARS", .font_size = 64.0f, .font_spec = spec,
            .tracking = TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, BASE_Y - 42.5f, 0.0f},
            .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
            .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_0"
        });
        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "ONE LETTER AT A TIME", .font_size = 76.0f, .font_spec = spec,
            .tracking = TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, BASE_Y + 42.5f, 0.0f},
            .start_delay = 36.0f, .duration = 8.0f, .stagger = 2.0f,
            .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_1"
        });

        return s.build();
    });
}

// ── 7. AnimTypewriterCursor — 2-line + cursor on line 2 ─────────────────
Composition anim_typewriter_cursor() {
    return composition({.name = "AnimTypewriterCursor", .width = 1920, .height = 1080, .duration = 110},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);

        // Precompute needed values BEFORE build_2line_typewriter
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TRACKING);
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TRACKING);
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
             .position({cursor_x, BASE_Y + 42.0f, 0.0f});

            // Blink: 6 frames on, 6 frames off, starting after cursor_delay
            auto& op = l.opacity_anim();
            op.key(Frame{0}, 0.0f, EasingCurve{Easing::Hold});
            op.key(Frame{static_cast<Frame>(cursor_delay)}, 0.0f, EasingCurve{Easing::Hold});
            op.key(Frame{static_cast<Frame>(cursor_delay)}, 1.0f, EasingCurve{Easing::Linear});
            for (f32 t = cursor_delay + 6.0f; t < 120.0f; t += 12.0f) {
                op.key(Frame{static_cast<Frame>(t)}, 0.0f, EasingCurve{Easing::Linear});
                op.key(Frame{static_cast<Frame>(t + 6.0f)}, 1.0f, EasingCurve{Easing::Linear});
            }

            TextSpec ts;
            ts.content.value = "|";
            ts.layout.box = {20.0f, BOX_H};
            ts.position = {0.0f, 0.0f, 0.0f};
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

// ── 8. AnimTypewriterSlide — 2-line, chars slide up ─────────────────────
Composition anim_typewriter_slide() {
    return composition({.name = "AnimTypewriterSlide", .width = 1920, .height = 1080, .duration = 100},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);

        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TRACKING);
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TRACKING);
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "THIS TEXT APPEARS", .font_size = 64.0f, .font_spec = spec,
            .tracking = TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, BASE_Y - 42.5f, 0.0f},
            .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
            .slide_up = true, .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_0"
        });
        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "ONE LETTER AT A TIME", .font_size = 76.0f, .font_spec = spec,
            .tracking = TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, BASE_Y + 42.5f, 0.0f},
            .start_delay = 36.0f, .duration = 8.0f, .stagger = 2.0f,
            .slide_up = true, .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .layer_prefix = "ch_1"
        });

        return s.build();
    });
}

// ── 9. AnimTypewriterGlow — 2-line typewriter reveal with glow ──────────
//
// Stable per-glyph approach: each character is pre-positioned at its final
// location using full-text layout metrics. Only opacity and glow animate.
// The text block stays perfectly stable throughout the animation.
//
Composition anim_typewriter_glow() {
    return composition({.name = "AnimTypewriterGlow", .width = 1920, .height = 1080, .duration = 160},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);

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

// ── 10. AnimTypewriterStagger — 4-line rhythmic stagger ─────────────────
Composition anim_typewriter_stagger() {
    return composition({.name = "AnimTypewriterStagger", .width = 1920, .height = 1080, .duration = 120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);

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
            f32 w = measure_text_width(lines[i].text, lines[i].size, spec, TRACKING);
            if (w > max_w) max_w = w;
        }
        f32 ref_x = -max_w * 0.5f;

        const f32 start_y = BASE_Y - 108.0f;
        const f32 step_y = 72.0f;

        for (int i = 0; i < 4; ++i) {
            build_text_reveal_line(s, TextRevealDescriptor{
                .text = lines[i].text, .font_size = lines[i].size, .font_spec = spec,
                .tracking = TRACKING, .ref_offset_x = ref_x,
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
