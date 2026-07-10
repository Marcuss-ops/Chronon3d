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
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/common/background_helpers.hpp"
#include "content/common/text_reveal_helpers.hpp"

#include <functional>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>

namespace chronon3d::content::anims {

// ── Typography constants (from shared headers) ──────────────────────────
using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::animation_helpers::SHADOW_COLOR;
using chronon3d::content::animation_helpers::FONT_REGULAR;
using chronon3d::content::animation_helpers::TextAnimBg;
using chronon3d::content::animation_helpers::make_text_anim;
using chronon3d::content::animation_helpers::text_anim_opacity;
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
        .placement = {TextPlacementKind::Absolute},
        .offset    = {0.0f, 0.0f},
    };
}

// ── make_easy_anim ─────────────────────────────────────────────────────────
// Shared helper for the 5 easy text animation compositions.  Encapsulates the
// common skeleton (grid bg, centered layer, drop shadow, 80pt text) and lets
// each composition supply only its animation-specific setup via a lambda.
//
// Previously each composition duplicated ~15 lines of identical boilerplate;
// now they are 5–10 lines of pure animation logic.


// F0.2 — shared_typewriter_engine() + current_path() static resolver REMOVED.
// Typewriter compositions now use ctx.font_engine from the runtime chain:
//   RenderEngine::set_assets_root() → Runtime::resolver() → FontEngine → FrameContext
// See docs/adr/ADR-020-shared-static-fontengine-singleton.md for migration rationale.

// ── Tracking constant (kept local for typewriter section below) ────
constexpr f32 TRACKING = 4.0f;

// Build a 2-line typewriter block — all lines share the SAME left edge
void build_2line_typewriter(SceneBuilder& s,
                            const std::string& line1, f32 size1,
                            const std::string& line2, f32 size2,
                            f32 start_delay_2 = 36.0f,
                            f32 line_spacing = 85.0f,
                            bool slide_up = false,
                            f32 glow_intensity = 0.0f)
{        auto spec = font_regular();
        f32 w1 = measure_text_width(line1, size1, spec, TRACKING, *s.font_engine());
        f32 w2 = measure_text_width(line2, size2, spec, TRACKING, *s.font_engine());
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
    // Original used p=min(progress*3,1)+interpolate(p,0,0.30,...): completes at frame 6.
    return make_text_anim("AnimSlideUp", txt_center("Slide Up", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.0f, BASE_Y + 80.0f, 0.0f})
                .to(Frame{6}, Vec3{0.0f, BASE_Y, 0.0f}, Easing::OutCubic)
                .apply_to(l.position_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// 2. AnimScalePop — scales from small with overshoot
Composition anim_scale_pop() {
    return make_text_anim("AnimScalePop", txt_center("Scale Pop", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.6f, 0.6f, 1.0f})
                .to(Frame{6}, Vec3{1.0f, 1.0f, 1.0f}, Easing::OutBack)
                .apply_to(l.scale_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// 3. AnimBlurFocus — blurry → sharp via focus_in
Composition anim_blur_focus() {
    // Declarative blur Timeline (24px → 0px) replaces the focus_in preset.
    return make_text_anim("AnimBlurFocus", txt_center("Blur Focus", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            l.opacity(1.0f);
            motion::timeline(24.0f)
                .to(Frame{20}, 0.0f, Easing::OutCubic)
                .apply_to(l.blur_anim());
        });
}

// 4. AnimSlideLeft — slides in from the left
Composition anim_slide_left() {
    return make_text_anim("AnimSlideLeft", txt_center("Slide Left", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{-120.0f, BASE_Y, 0.0f})
                .to(Frame{6}, Vec3{0.0f, BASE_Y, 0.0f}, Easing::OutCubic)
                .apply_to(l.position_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// 5. AnimBounceDrop — drops from above with bounce
Composition anim_bounce_drop() {
    // K=2.5: 0.30/K * 80 ≈ frame 9.6 (rounded to 10 for OutBounce),
    // opacity finishes at 0.5/K * 80 = frame 16.
    return make_text_anim("AnimBounceDrop", txt_center("Bounce Drop", 80.0f),
                          Frame{80}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.0f, -230.0f, 0.0f})
                .to(Frame{10}, Vec3{0.0f, BASE_Y, 0.0f}, Easing::OutBounce)
                .apply_to(l.position_anim());
            motion::timeline(0.0f)
                .to(Frame{16}, 1.0f, Easing::OutCubic)
                .apply_to(l.opacity_anim());
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
        s.font_engine(ctx.font_engine_or_null());

        // Precompute widths FIRST, then use inline build_text_reveal_line calls
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TRACKING, *s.font_engine());
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
        s.font_engine(ctx.font_engine_or_null());

        // Precompute needed values BEFORE build_2line_typewriter
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TRACKING, *s.font_engine());
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
            ts.placement = {TextPlacementKind::Absolute};
            ts.offset    = {0.0f, 0.0f};
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
        s.font_engine(ctx.font_engine_or_null());

        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f, spec, TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f, spec, TRACKING, *s.font_engine());
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

// ── 10. AnimTypewriterStagger — 4-line rhythmic stagger ─────────────────
Composition anim_typewriter_stagger() {
    return composition({.name = "AnimTypewriterStagger", .width = 1920, .height = 1080, .duration = 120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
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
            f32 w = measure_text_width(lines[i].text, lines[i].size, spec, TRACKING, *s.font_engine());
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
