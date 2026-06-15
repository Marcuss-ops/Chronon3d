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

#include <string>
#include <algorithm>
#include <vector>

namespace chronon3d::content::anims {

// ── Typography constants (from shared headers) ──────────────────────────
using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::animation_helpers::SHADOW_COLOR;
using chronon3d::content::animation_helpers::FONT_REGULAR;

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
TextParams txt_center(std::string text, f32 font_size = 72.0f) {
    return TextParams{
        .text = std::move(text),
        .size = {BOX_W, BOX_H},
        .pos = {0.0f, 0.0f, 0.0f},
        .font_path = FONT_REGULAR,
        .font_size = font_size,
        .color = TEXT_COLOR,
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .line_height = 1.22f,
        .tracking = 4.0f,
    };
}

// Smooth fade helper
f32 fade(Frame frame, f32 start, f32 duration) {
    f32 t = std::clamp((static_cast<f32>(frame) - start) / duration, 0.0f, 1.0f);
    return interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
}

// ── Local FontEngine (file-scoped singleton, avoids shared_font_engine issues) ─
static FontEngine& anim_font_engine() {
    static FontEngine engine;
    return engine;
}

// ── Measure full text width (FontEngine-accurate) ──────────────────────
// Returns total advance width INCLUDING tracking, matching layout_text's output.
constexpr f32 TRACKING = 4.0f;

f32 measure_text_width(const std::string& text, f32 font_size) {
    FontEngine& engine = anim_font_engine();
    FontSpec spec{FONT_REGULAR, "", 400};
    auto run = engine.shape_text(text, spec, font_size);
    if (!run) return 0.0f;
    // Add tracking per glyph (excluding last) to match layout_text cursor logic
    const size_t n = run->glyphs.size();
    return run->width + TRACKING * static_cast<f32>(n > 1 ? n - 1 : 0);
}

// ── Per-glyph typewriter line builder ────────────────────────────────────
//
// Lays out the FULL text once using TextAnimator metrics, then creates one
// layer per character at its FINAL position. Only opacity changes per frame.
// This eliminates all layout instability from substring-based approaches.
//
// ref_offset_x: shared starting X for ALL lines in a multi-line block.
//   Pass -max_width/2 so all lines align to the same left edge.

struct TypeChar {
    std::string ch;
    f32         center_x{0.0f};
    f32         width{0.0f};
};

std::vector<TypeChar> layout_text(const std::string& text, f32 font_size,
                                  f32 ref_offset_x = 0.0f)
{
    FontEngine& engine = anim_font_engine();
    FontSpec spec{FONT_REGULAR, "", 400};
    auto run = engine.shape_text(text, spec, font_size);
    if (!run || run->glyphs.empty()) return {};

    const f32 tracking = TRACKING;

    std::vector<TypeChar> result;
    result.reserve(run->glyphs.size());

    f32 cursor = ref_offset_x;
    for (const auto& gp : run->glyphs) {
        // Map glyph cluster back to the source text substring
        size_t start_byte = gp.cluster;
        size_t end_byte = text.size();
        for (const auto& other : run->glyphs) {
            if (other.cluster > start_byte) {
                end_byte = other.cluster;
                break;
            }
        }
        std::string ch = text.substr(start_byte, end_byte - start_byte);

        f32 ch_w = gp.advance_x;
        f32 cx = cursor + ch_w * 0.5f;
        result.push_back({ch, cx, ch_w});
        cursor += ch_w + tracking;
    }

    return result;
}

void build_typewriter_line(SceneBuilder& s, const std::string& text,
                           f32 font_size, f32 y_pos, f32 start_delay,
                           f32 duration = 8.0f, f32 stagger = 2.0f,
                           bool add_shadow = true, bool slide_up = false,
                           f32 glow_intensity = 0.0f,
                           int line_index = 0,
                           f32 ref_offset_x = 0.0f)
{
    auto chars = layout_text(text, font_size, ref_offset_x);
    if (chars.empty()) return;

    for (size_t i = 0; i < chars.size(); ++i) {
        const auto& tc = chars[i];
        if (tc.ch.empty() || tc.ch == " ") continue;

        const f32 delay = start_delay + static_cast<f32>(i) * stagger;
        const f32 end_frame = delay + duration;
        const f32 cx = tc.center_x;

        s.layer(std::string("ch_") + std::to_string(line_index) + "_" + std::to_string(i),
                [cx, y_pos, fs = font_size, ch = tc.ch, delay, end_frame,
                 add_shadow, slide_up, glow_intensity, tc_w = tc.width](LayerBuilder& l)
        {
            l.pin_to(Anchor::Center)
             .position({cx, y_pos, 0.0f});

            // Opacity animation: invisible → visible → held visible.
            // Pattern:
            //   0..delay  → invisible (Hold)
            //   delay..end → fade in (OutCubic)
            //   end+ → fully visible (KeyframeTrack holds last value)
            {
                auto& op = l.opacity_anim();
                op.key(Frame{0}, 0.0f, EasingCurve{Easing::Hold});
                op.key(Frame{static_cast<Frame>(delay)}, 0.0f, EasingCurve{Easing::OutCubic});
                op.key(Frame{static_cast<Frame>(end_frame)}, 1.0f, EasingCurve{Easing::Hold});
            }

            // Optional slide-up
            if (slide_up) {
                auto& pos = l.position_anim();
                pos.key(Frame{0}, Vec3{cx, y_pos + 24.0f, 0.0f}, EasingCurve{Easing::Hold});
                pos.key(Frame{static_cast<Frame>(delay)}, Vec3{cx, y_pos + 24.0f, 0.0f}, EasingCurve{Easing::OutCubic});
                pos.key(Frame{static_cast<Frame>(end_frame)}, Vec3{cx, y_pos, 0.0f}, EasingCurve{Easing::Linear});
            }

            // Micro drop shadow
            if (add_shadow) {
                l.drop_shadow(Vec2{0.0f, 3.0f}, SHADOW_COLOR, 6.0f);
            }

            // Optional glow — radius scales with font size
            if (glow_intensity > 0.01f) {
                const f32 glow_radius = std::max(5.0f, fs * 0.10f);
                l.glow(GlowParams{
                    .enabled = true,
                    .radius = glow_radius,
                    .intensity = glow_intensity,
                    .color = {0.87f, 0.92f, 1.0f, 1.0f},
                    .preserve_source = true,
                    .additive = true,
                });
            }

            // Single character text at pre-calculated position
            TextParams tp;
            tp.text = ch;
            tp.size = {tc_w + (glow_intensity > 0.01f ? 40.0f : 12.0f), BOX_H};
            tp.pos = {0.0f, 0.0f, 0.0f};
            tp.font_path = FONT_REGULAR;
            tp.font_size = fs;
            tp.color = TEXT_COLOR;
            tp.align = TextAlign::Center;
            tp.vertical_align = VerticalAlign::Middle;
            tp.line_height = 1.22f;
            tp.tracking = TRACKING;

            l.text("label", tp);
        });
    }
}

// Build a 2-line typewriter block — all lines share the SAME left edge
void build_2line_typewriter(SceneBuilder& s,
                            const std::string& line1, f32 size1,
                            const std::string& line2, f32 size2,
                            f32 start_delay_2 = 36.0f,
                            f32 line_spacing = 85.0f,
                            bool slide_up = false,
                            f32 glow_intensity = 0.0f)
{
    // Find the widest line to use as alignment reference
    f32 w1 = measure_text_width(line1, size1);
    f32 w2 = measure_text_width(line2, size2);
    f32 max_w = std::max(w1, w2);
    f32 ref_x = -max_w * 0.5f;

    build_typewriter_line(s, line1, size1, BASE_Y - line_spacing * 0.5f,
                          0.0f, 8.0f, 2.0f, true, slide_up, glow_intensity, 0, ref_x);
    build_typewriter_line(s, line2, size2, BASE_Y + line_spacing * 0.5f,
                          start_delay_2, 8.0f, 2.0f, true, slide_up, glow_intensity, 1, ref_x);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  5 EASY ANIMATIONS (single text layer)
// ═══════════════════════════════════════════════════════════════════════════

// ── 1. AnimSlideUp ──────────────────────────────────────────────────────
Composition anim_slide_up() {
    return composition({.name = "AnimSlideUp", .width = 1920, .height = 1080, .duration = 60},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
        const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
        s.layer("text", [p, eased](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, BASE_Y + (1.0f - eased) * 80.0f, 0.0f})
             .opacity(p)
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", txt_center("Slide Up", 80.0f));
        });
        return s.build();
    });
}

// ── 2. AnimScalePop ─────────────────────────────────────────────────────
Composition anim_scale_pop() {
    return composition({.name = "AnimScalePop", .width = 1920, .height = 1080, .duration = 60},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
        const f32 sv = interpolate(p, 0.0f, 0.30f, 0.6f, 1.0f, Easing::OutBack);
        s.layer("text", [p, sv](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, BASE_Y, 0.0f})
             .opacity(p)
             .scale({sv, sv, 1.0f})
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", txt_center("Scale Pop", 80.0f));
        });
        return s.build();
    });
}

// ── 3. AnimBlurFocus ────────────────────────────────────────────────────
Composition anim_blur_focus() {
    return composition({.name = "AnimBlurFocus", .width = 1920, .height = 1080, .duration = 60},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, BASE_Y, 0.0f})
             .opacity(1.0f)
             .focus_in(24.0f, Frame{20})
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", txt_center("Blur Focus", 80.0f));
        });
        return s.build();
    });
}

// ── 4. AnimSlideLeft ────────────────────────────────────────────────────
Composition anim_slide_left() {
    return composition({.name = "AnimSlideLeft", .width = 1920, .height = 1080, .duration = 60},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 3.0f);
        const f32 eased = interpolate(p, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
        s.layer("text", [p, eased](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({(1.0f - eased) * -120.0f, BASE_Y, 0.0f})
             .opacity(p)
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", txt_center("Slide Left", 80.0f));
        });
        return s.build();
    });
}

// ── 5. AnimBounceDrop ───────────────────────────────────────────────────
Composition anim_bounce_drop() {
    return composition({.name = "AnimBounceDrop", .width = 1920, .height = 1080, .duration = 80},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
        const f32 p = std::min(1.0f, static_cast<f32>(ctx.progress()) * 2.5f);
        const f32 y_offset = interpolate(p, 0.0f, 0.30f, -230.0f, BASE_Y, Easing::OutBounce);
        s.layer("text", [p, y_offset](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .opacity(std::min(1.0f, p * 2.0f))
             .position({0.0f, y_offset, 0.0f})
             .drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
             .text("label", txt_center("Bounce Drop", 80.0f));
        });
        return s.build();
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

        // Precompute widths FIRST, then use inline build_typewriter_line calls
        // (avoiding FontEngine calls nested inside build_2line_typewriter)
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f);
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f);
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        build_typewriter_line(s, "THIS TEXT APPEARS", 64.0f, BASE_Y - 42.5f,
                              0.0f, 8.0f, 2.0f, true, false, 0.0f, 0, ref_x);
        build_typewriter_line(s, "ONE LETTER AT A TIME", 76.0f, BASE_Y + 42.5f,
                              36.0f, 8.0f, 2.0f, true, false, 0.0f, 1, ref_x);

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
        // to avoid any FontEngine-interaction issues.
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f);
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f);
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

            TextParams tp;
            tp.text = "|";
            tp.size = {20.0f, BOX_H};
            tp.pos = {0.0f, 0.0f, 0.0f};
            tp.font_path = FONT_REGULAR;
            tp.font_size = 76.0f;
            tp.color = TEXT_COLOR;
            tp.align = TextAlign::Center;
            tp.vertical_align = VerticalAlign::Middle;
            tp.line_height = 1.22f;

            l.text("cursor_label", tp);
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

        f32 w1 = measure_text_width("THIS TEXT APPEARS", 64.0f);
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 76.0f);
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        build_typewriter_line(s, "THIS TEXT APPEARS", 64.0f, BASE_Y - 42.5f,
                              0.0f, 8.0f, 2.0f, true, true, 0.0f, 0, ref_x);
        build_typewriter_line(s, "ONE LETTER AT A TIME", 76.0f, BASE_Y + 42.5f,
                              36.0f, 8.0f, 2.0f, true, true, 0.0f, 1, ref_x);

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
        f32 max_w = 0.0f;
        for (int i = 0; i < 4; ++i) {
            f32 w = measure_text_width(lines[i].text, lines[i].size);
            if (w > max_w) max_w = w;
        }
        f32 ref_x = -max_w * 0.5f;

        const f32 start_y = BASE_Y - 108.0f;
        const f32 step_y = 72.0f;

        for (int i = 0; i < 4; ++i) {
            build_typewriter_line(s, lines[i].text, lines[i].size,
                                  start_y + static_cast<f32>(i) * step_y,
                                  lines[i].delay,
                                  8.0f, 3.0f, true, false, 0.0f, i, ref_x);
        }

        return s.build();
    });
}

} // namespace chronon3d::content::anims
