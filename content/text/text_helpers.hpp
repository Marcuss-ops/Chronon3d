#pragma once

// ── Shared Text Helpers ─────────────────────────────────────────────────────
//
// Canonical text-building helpers used by ALL composition families.
// Three small functions that cover 95% of content text needs:
//
//   1. centered_text()   — one-liner centered text with auto-fit
//   2. typewriter_text() — character-by-character reveal
//   3. glow_text()       — text params tuned for glow layers
//
// Each returns a plain TextParams that can be passed directly to
// LayerBuilder::text().  Callers compose with the existing glow / shadow APIs:
//
//   #include "content/text/text_helpers.hpp"
//
//   SceneBuilder s(ctx);
//   s.layer("hero", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
//       content::text::glow::apply_ae_glow(l);  // from content/common/text_helpers.hpp
//       l.text("t", content::text::centered_text({
//           .text      = "TITLE",
//           .font_size = 100,
//           .tracking  = 6,
//       }));
//   });
//
// Namespace: chronon3d::content::text
// Same namespace as content/text/text_theme.hpp — constants are shared.
//

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
// CenterTextOptions — unified options struct for all text helpers
// ═════════════════════════════════════════════════════════════════════════════
//
// Every helper accepts this by value (small, fits in registers) and consumes
// the `text` member via std::move.
//
struct CenterTextOptions {
    std::string text;                     // text content (consumed)
    Vec2  box{1200.0f, 240.0f};           // logical text box
    Vec3  pos{0.0f, 0.0f, 0.0f};          // position (anchor is always Center)
    std::string font_path{"assets/fonts/Poppins-Bold.ttf"};
    std::string font_family{"Poppins"};
    int   font_weight{700};
    std::string font_style{"normal"};
    f32   font_size{96.0f};               // target font size
    f32   tracking{0.0f};                 // extra px per glyph
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    int   max_lines{1};                   // 0 = unlimited
    bool  auto_fit{false};                 // shrink to fit box (off for predictable sizing)
    f32   line_height{0.95f};             // multiplier of font-size
    f32   min_font_size{12.0f};           // floor for auto-fit
    f32   max_font_size{160.0f};          // ceiling for auto-fit
};

// ═════════════════════════════════════════════════════════════════════════════
// 1. centered_text
// ═════════════════════════════════════════════════════════════════════════════
//
// Returns TextParams for centered, middle-aligned text inside `box`.
// Anchor is always TextAnchor::Center so pos maps to box center.
// Uses PixelInk centering mode for optical centering (accounts for glyph
// asymmetries that layout-box centering misses).
//
// Example:
//   l.text("t", centered_text({.text = "TITLE", .font_size = 96, .tracking = 6}));
//
inline TextParams centered_text(CenterTextOptions o) {
    return TextParams{
        .text            = std::move(o.text),
        .size            = o.box,
        .pos             = o.pos,
        .font_path       = std::move(o.font_path),
        .font_family     = std::move(o.font_family),
        .font_weight     = o.font_weight,
        .font_style      = std::move(o.font_style),
        .font_size       = o.font_size,
        .color           = o.color,
        .anchor          = TextAnchor::Center,
        .centering_mode  = TextCenteringMode::PixelInk,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = o.line_height,
        .tracking        = o.tracking,
        .auto_fit        = o.auto_fit,
        .max_lines       = o.max_lines,
        .min_font_size   = o.min_font_size,
        .max_font_size   = o.max_font_size,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::Word,
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. typewriter_text
// ═════════════════════════════════════════════════════════════════════════════
//
// Like centered_text but reveals `text` character-by-character based on
// the current frame.  Returns a single space (" ") for frame 0 so the
// text layer never collapses to zero size.
//
// TypewriterOptions gives full control over the reveal:
//   - easing:       curve applied to the linear progress (default Linear)
//   - start_delay:  frames to wait before the first character appears
//   - fade_chars:   number of "virtual" characters used for the soft-alpha
//                   transition at the leading edge.  1.0 = one char fades;
//                   0.0 = hard reveal (instant per character, same as before).
//
// Example:
//   l.text("t", typewriter_text({.text = "HELLO", .font_size = 72},
//           ctx.frame(), 2.0f, {.easing = EasingCurve{Easing::OutCubic},
//                                .start_delay = 10}));
//
struct TypewriterOptions {
    EasingCurve easing{Easing::Linear};
    Frame       start_delay{0};
    f32         fade_chars{1.0f};
};

inline TextParams typewriter_text(CenterTextOptions o,
                                  Frame frame,
                                  f32 chars_per_frame = 1.5f,
                                  TypewriterOptions tw = {}) {
    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(tw.start_delay);
    const f32 total_chars_f = static_cast<f32>(o.text.size());

    // Nothing visible yet (before delay or at frame 0)
    if (raw_frame < 0.0f || total_chars_f <= 0.0f) {
        std::string space(" ");
        Color c = o.color;
        c.a = 0.0f;
        return TextParams{
            .text = std::move(space), .size = o.box, .pos = o.pos,
            .font_path = std::move(o.font_path), .font_family = std::move(o.font_family),
            .font_weight = o.font_weight, .font_style = std::move(o.font_style),
            .font_size = o.font_size, .color = c,
            .anchor = TextAnchor::Center, .centering_mode = TextCenteringMode::PixelInk,
            .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle,
            .line_height = o.line_height, .tracking = o.tracking,
            .auto_fit = o.auto_fit, .max_lines = o.max_lines,
            .min_font_size = o.min_font_size, .max_font_size = o.max_font_size,
            .overflow = TextOverflow::Clip, .wrap = TextWrap::Word,
        };
    }

    // Linear progress → apply easing
    const f32 linear_t = std::clamp(raw_frame * chars_per_frame / total_chars_f,
                                    0.0f, 1.0f);
    const f32 eased_t  = tw.easing.apply(linear_t);

    // Fully revealed → return complete text at full color
    if (eased_t >= 1.0f) {
        return TextParams{
            .text = std::move(o.text), .size = o.box, .pos = o.pos,
            .font_path = std::move(o.font_path), .font_family = std::move(o.font_family),
            .font_weight = o.font_weight, .font_style = std::move(o.font_style),
            .font_size = o.font_size, .color = o.color,
            .anchor = TextAnchor::Center, .centering_mode = TextCenteringMode::PixelInk,
            .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle,
            .line_height = o.line_height, .tracking = o.tracking,
            .auto_fit = o.auto_fit, .max_lines = o.max_lines,
            .min_font_size = o.min_font_size, .max_font_size = o.max_font_size,
            .overflow = TextOverflow::Clip, .wrap = TextWrap::Word,
        };
    }

    const size_t revealed = static_cast<size_t>(eased_t * total_chars_f);
    const f32    partial  = (eased_t * total_chars_f) - static_cast<f32>(revealed);

    std::string visible = (revealed == 0)
        ? std::string(" ")
        : o.text.substr(0, revealed);

    // Fade: during a character transition the layer alpha dips, creating a
    // soft leading-edge glow.  fade_chars is clamped to [0,1] so the text
    // never goes fully dark; a floor of 0.25 keeps it readable at all times.
    Color c = o.color;
    if (tw.fade_chars > 0.0f && revealed < o.text.size() && revealed > 0) {
        const f32 fade_range = std::clamp(tw.fade_chars, 0.0f, 1.0f);
        const f32 fade_t = std::clamp(1.0f - fade_range * partial, 0.25f, 1.0f);
        c.a *= fade_t;
    }

    return TextParams{
        .text            = std::move(visible),
        .size            = o.box,
        .pos             = o.pos,
        .font_path       = std::move(o.font_path),
        .font_family     = std::move(o.font_family),
        .font_weight     = o.font_weight,
        .font_style      = std::move(o.font_style),
        .font_size       = o.font_size,
        .color           = c,
        .anchor          = TextAnchor::Center,
        .centering_mode  = TextCenteringMode::PixelInk,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = o.line_height,
        .tracking        = o.tracking,
        .auto_fit        = o.auto_fit,
        .max_lines       = o.max_lines,
        .min_font_size   = o.min_font_size,
        .max_font_size   = o.max_font_size,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::Word,
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. typewriter_build — stable per-character typewriter
// ═════════════════════════════════════════════════════════════════════════════
//
// Unlike typewriter_text() which uses substr(0, n) and causes reflow every
// frame, this function:
//   1. Computes the FULL layout once (word-wrap, line breaks, positions)
//   2. Creates one layer per visible character with per-character opacity
//   3. Already-revealed characters are white, opaque, and NEVER move
//   4. Only the currently-revealing character has a fade
//
// Usage:
//   text::typewriter_build(s, "tw", {
//       .text = "The details are not the details.",
//       .box = {1100.0f, 350.0f},
//       .font_size = 58.0f,
//       .tracking = 4.0f,
//       .chars_per_frame = 0.35f,
//       .start_delay = Frame{20},
//       .easing = EasingCurve{Easing::InOutCubic},
//   }, ctx.frame);
//

struct TypewriterBuildOptions {
    std::string text;
    Vec2  box{1200.0f, 240.0f};
    f32   font_size{96.0f};
    f32   tracking{0.0f};
    f32   line_height{1.10f};
    std::string font_path{"assets/fonts/Poppins-Bold.ttf"};
    std::string font_family{"Poppins"};
    int   font_weight{700};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32   chars_per_frame{1.0f};
    Frame start_delay{0};
    EasingCurve easing{Easing::Linear};
    EasingCurve fade_easing{Easing::OutCubic};
    Frame fade_duration{10};
};

// ── Per-character layout result ──────────────────────────────────────────
struct TypewriterCharPos {
    size_t byte_offset;  // offset into the source text (UTF-8)
    size_t byte_len;     // length in bytes of this character
    f32 x;               // center-x of this character (relative to text block center)
    f32 y;               // center-y of this line (relative to text block center)
};

struct TypewriterLayout {
    std::vector<TypewriterCharPos> chars;
    f32 total_width{0.0f};
    f32 total_height{0.0f};
};

// ── Compute the full layout once (stable across all frames) ──────────────
//
// This uses a simple heuristic for character widths (font_size * 0.58 +
// tracking).  The same heuristic is used for word-wrap decisions and for
// x-positioning, so the layout is self-consistent even if the absolute
// widths differ slightly from the renderer.
//
inline TypewriterLayout compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height)
{
    TypewriterLayout result;
    if (text.empty()) return result;

    constexpr f32 kCharRatio = 0.58f;
    constexpr f32 kSpaceRatio = 0.28f;

    auto char_w = [&](size_t byte_pos) -> f32 {
        unsigned char c = static_cast<unsigned char>(text[byte_pos]);
        if (c == ' ') return font_size * kSpaceRatio + tracking;
        if (c >= 0xC0) return font_size * 0.65f + tracking; // multi-byte conservative
        return font_size * kCharRatio + tracking;
    };

    auto utf8_len = [&](size_t pos) -> size_t {
        unsigned char c = static_cast<unsigned char>(text[pos]);
        if (c < 0x80) return 1;
        if (c < 0xE0) return 2;
        if (c < 0xF0) return 3;
        return 4;
    };

    // ── Word-wrap into lines ────────────────────────────────────────────
    struct Line { size_t begin_idx; size_t end_idx; f32 width; };
    std::vector<Line> lines;

    size_t line_start = 0;      // byte offset of current line start
    f32    line_width = 0.0f;   // accumulated width of current line
    size_t word_boundary = 0;   // byte offset AFTER the last space
    f32    width_at_boundary = 0.0f;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t clen = utf8_len(pos);
        f32 cw = char_w(pos);

        bool is_space = (clen == 1 && text[pos] == ' ');
        bool is_newline = (clen == 1 && text[pos] == '\n');

        if (is_newline) {
            // Explicit line break: finalize current line, skip the newline char
            lines.push_back({line_start, pos, line_width});
            pos += clen;
            line_start = pos;
            line_width = 0.0f;
            word_boundary = pos;
            width_at_boundary = 0.0f;
            continue;
        }

        if (is_space) {
            word_boundary = pos + clen;
            width_at_boundary = line_width + cw;
        }

        if (line_width + cw > box.x && pos > line_start) {
            // Wrap needed
            if (word_boundary > line_start) {
                // Wrap at last word boundary
                lines.push_back({line_start, word_boundary - 1, width_at_boundary - char_w(word_boundary - 1)});
                line_start = word_boundary;
                // Recalculate width from line_start to current pos
                line_width = 0.0f;
                size_t p2 = line_start;
                while (p2 < pos) { line_width += char_w(p2); p2 += utf8_len(p2); }
                word_boundary = line_start;
                width_at_boundary = 0.0f;
            } else {
                // No word boundary on this line — force break
                lines.push_back({line_start, pos, line_width});
                line_start = pos;
                line_width = 0.0f;
                word_boundary = pos;
                width_at_boundary = 0.0f;
            }
        }

        line_width += cw;
        pos += clen;
    }
    // Final line
    if (line_start < text.size()) {
        // Trim trailing spaces
        size_t end = text.size();
        while (end > line_start && end - 1 < text.size() && text[end - 1] == ' ') {
            size_t prev = end - 1;
            // Find start of this char
            while (prev > 0 && (static_cast<unsigned char>(text[prev]) & 0xC0) == 0x80) --prev;
            line_width -= char_w(prev);
            end = prev;
        }
        lines.push_back({line_start, end, line_width});
    }

    if (lines.empty()) return result;

    // ── Compute max line width for centering ────────────────────────────
    f32 max_w = 0.0f;
    for (auto& ln : lines) max_w = std::max(max_w, ln.width);

    f32 total_h = static_cast<f32>(lines.size()) * line_height;
    result.total_width = max_w;
    result.total_height = total_h;

    // ── Assign per-character positions ──────────────────────────────────
    f32 y_base = -total_h * 0.5f + line_height * 0.5f;

    for (size_t li = 0; li < lines.size(); ++li) {
        auto& ln = lines[li];
        f32 x = -ln.width * 0.5f; // center each line
        f32 y = y_base + static_cast<f32>(li) * line_height;

        size_t p = ln.begin_idx;
        while (p < ln.end_idx && p < text.size()) {
            size_t clen = utf8_len(p);
            f32 cw = char_w(p);

            TypewriterCharPos cp;
            cp.byte_offset = p;
            cp.byte_len = clen;
            cp.x = x + cw * 0.5f;
            cp.y = y;
            result.chars.push_back(cp);

            x += cw;
            p += clen;
        }
    }

    return result;
}

// ── typewriter_build — create per-character layers ───────────────────────
//
// Creates one layer per visible character.  Already-revealed characters are
// fully opaque and NEVER move.  Only the currently-revealing character fades.
//
inline void typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame)
{
    // 1. Compute the full layout once (stable, same every frame)
    auto layout = compute_typewriter_layout(
        opts.text, opts.font_size, opts.tracking,
        opts.box, opts.line_height);

    if (layout.chars.empty()) return;

    const f32 total_chars = static_cast<f32>(layout.chars.size());
    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(opts.start_delay);

    // Before start_delay: nothing visible
    if (raw_frame < 0.0f) return;

    // 2. Compute reveal progress with easing
    const f32 linear_t = std::clamp(raw_frame * opts.chars_per_frame / total_chars, 0.0f, 1.0f);
    const f32 eased_t = opts.easing.apply(linear_t);
    const f32 revealed_exact = eased_t * total_chars;
    const size_t revealed_count = static_cast<size_t>(revealed_exact);
    const f32 revealed_frac = revealed_exact - static_cast<f32>(revealed_count);

    // 3. Create one layer per visible character
    for (size_t i = 0; i < layout.chars.size(); ++i) {
        auto& cp = layout.chars[i];

        // Skip space characters (no visible glyph)
        if (cp.byte_len == 1 && opts.text[cp.byte_offset] == ' ') continue;
        // Skip newlines
        if (cp.byte_len == 1 && opts.text[cp.byte_offset] == '\n') continue;

        // Determine opacity
        f32 opacity;
        if (i < revealed_count) {
            // Already revealed: fully opaque
            opacity = 1.0f;
        } else if (i == revealed_count) {
            // Currently revealing: fade in
            opacity = opts.fade_easing.apply(revealed_frac);
        } else {
            // Not yet revealed: stop (chars are in order)
            break;
        }

        if (opacity < 0.005f) continue;

        // Extract the character glyph
        std::string glyph = opts.text.substr(cp.byte_offset, cp.byte_len);

        // Create a layer for this character
        std::string lname = std::string(layer_prefix) + "_c" + std::to_string(i);

        // Use the per-character position (relative to text block center)
        // pin_to(Anchor::Center) places the layer at canvas center
        // .pos offsets from canvas center to the character's position
        s.layer(lname, [&](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity(opacity);

            TextParams tp;
            tp.text = glyph;
            tp.size = {opts.font_size * 2.0f, opts.font_size * 2.0f};
            tp.pos = {cp.x, cp.y, 0.0f};
            tp.font_path = opts.font_path;
            tp.font_family = opts.font_family;
            tp.font_weight = opts.font_weight;
            tp.font_size = opts.font_size;
            tp.color = opts.color;
            tp.anchor = TextAnchor::Center;
            tp.centering_mode = TextCenteringMode::PixelInk;
            tp.align = TextAlign::Center;
            tp.vertical_align = VerticalAlign::Middle;
            tp.line_height = opts.line_height;
            tp.tracking = 0.0f; // spacing already handled by layout
            tp.wrap = TextWrap::None;
            tp.overflow = TextOverflow::Clip;

            l.text("glyph", tp);
        });
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. glow_text
// ═════════════════════════════════════════════════════════════════════════════
//
// Like centered_text but tuned for use with glow effects.  Currently
// identical to centered_text — the glow effect itself is applied via
// LayerBuilder::glow() or glow::apply_ae_glow().  This helper exists as
// a semantic marker so callers can self-document "this text is meant to
// work with a glow" and may later set glow-specific defaults (e.g. tighter
// line-height, different overflow handling, or padding for bloom).
//
// Example:
//   glow::apply_ae_glow(l);
//   l.text("t", glow_text({.text = "GLOW", .font_size = 80, .tracking = 4}));
//
inline TextParams glow_text(CenterTextOptions o,
                            Color /*glow_color*/ = {1.0f, 1.0f, 1.0f, 1.0f},
                            f32 /*radius*/ = 24.0f,
                            f32 /*intensity*/ = 0.6f) {
    // glow_color / radius / intensity are reserved for future use when
    // TextParams carries glow metadata.  For now, glow is applied via
    // the layer builder API.
    return TextParams{
        .text            = std::move(o.text),
        .size            = o.box,
        .pos             = o.pos,
        .font_path       = std::move(o.font_path),
        .font_family     = std::move(o.font_family),
        .font_weight     = o.font_weight,
        .font_style      = std::move(o.font_style),
        .font_size       = o.font_size,
        .color           = o.color,
        .anchor          = TextAnchor::Center,
        .centering_mode  = TextCenteringMode::PixelInk,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = o.line_height,
        .tracking        = o.tracking,
        .auto_fit        = o.auto_fit,
        .max_lines       = o.max_lines,
        .min_font_size   = o.min_font_size,
        .max_font_size   = o.max_font_size,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::Word,
    };
}

} // namespace chronon3d::content::text
