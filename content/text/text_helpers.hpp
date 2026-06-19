#pragma once

// ── Shared Text Helpers ─────────────────────────────────────────────────────
//
// Canonical text-building helpers used by ALL composition families.
// Three small functions that cover 95% of content text needs:
//
//   1. centered_text()   — one-liner centered text with auto-fit
//   2. typewriter_text() — character-by-character reveal (simple substr)
//   3. typewriter_build  — per-character layers with stable layout
//   4. glow_text()       — text params tuned for glow layers
//
// Each returns a plain TextParams that can be passed directly to
// LayerBuilder::text().  Callers compose with the existing glow / shadow APIs:
//
//   #include "content/text/text_helpers.hpp"
//
//   SceneBuilder s(ctx);
//   s.layer("hero", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
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
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
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

// ── UTF-8 code-point-safe helpers ──────────────────────────────────────

/// Count Unicode code points (not bytes) in a UTF-8 string.
/// Continuation bytes (10xxxxxx) are skipped.
/// Prefer grapheme_cluster_count() for user-visible characters —
/// this function is kept for backward compatibility.
inline size_t utf8_code_point_count(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

/// Find the byte offset after exactly N complete code points.
/// Returns s.size() if N exceeds the string length.  Safe for use with
/// std::string::substr() — never splits a multi-byte character.
///
/// Walks the entire multi-byte sequence before returning the offset,
/// so that substr(0, offset) is always valid UTF-8.
/// Prefer grapheme_byte_offset_at() for user-visible characters —
/// this function is kept for backward compatibility.
inline size_t utf8_byte_offset_at(const std::string& s, size_t code_points) {
    size_t offset = 0;
    size_t count  = 0;

    while (offset < s.size() && count < code_points) {
        const unsigned char lead = static_cast<unsigned char>(s[offset]);

        // Determine sequence length from the leading byte
        size_t len = 1;
        if      ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;

        // Safety: clamp if the sequence extends past the end of the string
        if (offset + len > s.size()) len = 1;

        offset += len;
        ++count;
    }

    return offset;
}

// ── Grapheme-cluster-safe helpers (UAX #29) ──────────────────────────
//
// These delegate to the canonical implementation in
// chronon3d::detail (text_layout_engine.hpp) so there is one
// source of truth for grapheme cluster boundaries.

using chronon3d::detail::grapheme_cluster_count;
using chronon3d::detail::grapheme_byte_offset_at;
using chronon3d::detail::is_grapheme_extend;
using chronon3d::detail::utf8_decode_cp;

// ═════════════════════════════════════════════════════════════════════════════
// 2. typewriter_text — simple substr-based reveal
// ═════════════════════════════════════════════════════════════════════════════
//
// Like centered_text but reveals `text` character-by-character based on
// the current frame.  Returns a single space (" ") for frame 0 so the
// text layer never collapses to zero size.
//
// **Grapheme-cluster safe**: uses UAX #29 grapheme cluster counting, so
// combining marks, ZWJ emoji sequences, and emoji skin-tone modifiers are
// never split from their base character during the reveal.
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
    const f32 total_chars_f = static_cast<f32>(grapheme_cluster_count(o.text));

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
        : o.text.substr(0, grapheme_byte_offset_at(o.text, revealed));

    // Fade: during a character transition the layer alpha dips, creating a
    // soft leading-edge glow.  fade_chars is clamped to [0,1] so the text
    // never goes fully dark; a floor of 0.25 keeps it readable at all times.
    Color c = o.color;
    if (tw.fade_chars > 0.0f && revealed < static_cast<size_t>(total_chars_f) && revealed > 0) {
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
// 3. typewriter_build — stable per-character typewriter
// ═════════════════════════════════════════════════════════════════════════════
//
// Unlike typewriter_text() which uses substr(0, n) and causes reflow every
// frame, this function:
//   1. Computes the FULL layout once via HarfBuzz shaping (cached)
//   2. Creates one layer per visible character with per-character opacity
//   3. Already-revealed characters are white, opaque, and NEVER move
//   4. Only the currently-revealing character has a fade
//
// Uses pixel-accurate advances from FontEngine::shape_text(), calibrated via
// hb_ft_font_changed() in font_engine.cpp.
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
    f32 advance;         // pixel advance of this character (including tracking)
};

struct TypewriterLayout {
    std::vector<TypewriterCharPos> chars;
    f32 total_width{0.0f};
    f32 total_height{0.0f};
};

// ── Compute the full layout once (stable across all frames) ──────────────
//
// Uses FontEngine::shape_text() for pixel-accurate cluster mapping.
// advance_x values are correct because font_engine.cpp calls
// hb_ft_font_changed() after FT_Set_Pixel_Sizes.
//
/// Optional output parameter: when non-null, the fully resolved
/// PlacedGlyphRun is written here (avoids double-shaping when caller
/// also needs the pre-shaped glyphs, e.g. typewriter_build).
inline TypewriterLayout compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height,
    const FontSpec& font_spec,
    PlacedGlyphRun* out_placed = nullptr)
{
    TypewriterLayout result;
    if (text.empty()) return result;

    auto& engine = shared_font_engine();

    auto run = engine.shape_text(text, font_spec, font_size);
    if (!run || run->glyphs.empty()) return result;

    // ── Canonical PlacedGlyphRun: resolves positions and builds
    // cluster info (byte offsets, advances with tracking). ───────────
    auto placed = resolve_placed_glyph_run(*run, tracking, text);
    if (placed.clusters.empty()) return result;

    // ── Convert PlacedGlyphRun::Cluster → CharAdv for grapheme merge ──
    // Use raw_advance (without per-HarfBuzz-cluster tracking) so that
    // tracking is applied cleanly at the grapheme level below.
    struct CharAdv { size_t byte_offset; size_t byte_len; f32 advance; };
    std::vector<CharAdv> char_advances;
    char_advances.reserve(placed.clusters.size());
    for (const auto& cl : placed.clusters) {
        CharAdv ca;
        ca.byte_offset = cl.byte_offset;
        ca.byte_len = cl.byte_len;
        ca.advance = cl.raw_advance;  // raw advance — NO per-HB-cluster tracking
        char_advances.push_back(ca);
    }

    // ── Grapheme-cluster merge: group glyphs that form a single
    //     user-perceived character (e.g. base + combining mark). ───
    // This is a safety net for cases where HarfBuzz assigns different
    // cluster values to grapheme extenders (ZWJ sequences, skin tones).
    {
        std::vector<CharAdv> merged;
        merged.reserve(char_advances.size());

        // GB11 state for ZWJ emoji sequences
        enum class GB11State { Normal, ExtPict, ExtPictZWJ };
        GB11State gb11 = GB11State::Normal;

        for (size_t i = 0; i < char_advances.size(); ++i) {
            auto& ca = char_advances[i];

            size_t cp_offset = ca.byte_offset;
            if (cp_offset < text.size()) {
                const char32_t cp = utf8_decode_cp(std::string_view(text), cp_offset);
                const bool is_ext = is_grapheme_extend(cp);
                const bool is_ep  = chronon3d::detail::is_extended_pictographic(cp);
                const bool is_ri  = chronon3d::detail::is_regional_indicator(cp);
                const bool is_zwj = (cp == 0x200D);

                // RI pair: merge two consecutive Regional Indicators
                if (!merged.empty() && is_ri) {
                    size_t prev_off = merged.back().byte_offset;
                    size_t prev_off2 = prev_off;
                    if (prev_off2 < text.size()) {
                        const char32_t prev_cp = utf8_decode_cp(
                            std::string_view(text), prev_off2);
                        if (chronon3d::detail::is_regional_indicator(prev_cp)) {
                            auto& prev = merged.back();
                            prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                            prev.advance += ca.advance;
                            gb11 = GB11State::Normal;
                            continue;
                        }
                    }
                }

                // ZWJ after ExtPict: transition state, merge
                if (is_zwj && gb11 == GB11State::ExtPict) {
                    gb11 = GB11State::ExtPictZWJ;
                    if (!merged.empty()) {
                        auto& prev = merged.back();
                        prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                        prev.advance += ca.advance;
                    }
                    continue;
                }

                // ExtPict after ExtPictZWJ: merge (GB11 continuation)
                if (is_ep && !is_ri && gb11 == GB11State::ExtPictZWJ) {
                    gb11 = GB11State::ExtPict;
                    if (!merged.empty()) {
                        auto& prev = merged.back();
                        prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                        prev.advance += ca.advance;
                    }
                    continue;
                }

                // Generic grapheme extender: merge into previous cluster
                if (is_ext && !merged.empty()) {
                    auto& prev = merged.back();
                    prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                    prev.advance += ca.advance;
                    continue;
                }

                // Update GB11 state for non-extend characters
                if (!is_ext) {
                    gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
                }
            }

            merged.push_back(ca);
        }

        char_advances = std::move(merged);
    }

    // ⚠  Tracking is NOT applied before wrapping.  Applying tracking to
    // the full sequence and then wrapping causes the last character of
    // each line to retain the gap intended for the first character of the
    // next line — an error visible at every line break.
    // Tracking is applied PER LINE after the wrap step below.

    // Word-wrap
    struct Line { size_t begin_idx; size_t end_idx; f32 width; };
    std::vector<Line> lines;

    size_t line_start = 0;
    f32    line_width = 0.0f;
    size_t word_boundary = 0;
    f32    width_at_boundary = 0.0f;

    for (size_t i = 0; i < char_advances.size(); ++i) {
        auto& ca = char_advances[i];
        bool is_space = (ca.byte_len == 1 && text[ca.byte_offset] == ' ');
        bool is_newline = (ca.byte_len == 1 && text[ca.byte_offset] == '\n');

        if (is_newline) {
            lines.push_back({line_start, i, line_width});
            line_start = i + 1;
            line_width = 0.0f;
            word_boundary = i + 1;
            width_at_boundary = 0.0f;
            continue;
        }
        if (is_space) {
            word_boundary = i + 1;
            width_at_boundary = line_width + ca.advance;
        }
        if (line_width + ca.advance > box.x && i > line_start) {
            if (word_boundary > line_start) {
                f32 lw = width_at_boundary;
                if (word_boundary > 0) lw -= char_advances[word_boundary - 1].advance;
                lines.push_back({line_start, word_boundary - 1, lw});
                line_start = word_boundary;
                line_width = 0.0f;
                for (size_t j = word_boundary; j < i; ++j)
                    line_width += char_advances[j].advance;
                word_boundary = line_start;
                width_at_boundary = 0.0f;
            } else {
                lines.push_back({line_start, i, line_width});
                line_start = i;
                line_width = 0.0f;
                word_boundary = i;
                width_at_boundary = 0.0f;
            }
        }
        line_width += ca.advance;
    }
    if (line_start < char_advances.size())
        lines.push_back({line_start, char_advances.size(), line_width});

    // ── Apply tracking PER LINE (after wrapping) ──────────────────
    // Tracking adds `tracking` pixels between each adjacent pair of
    // grapheme clusters within a line.  The last character of a line
    // does NOT receive tracking because the gap to the next line is
    // handled by the line-step (no gap between lines for tracking).
    for (auto& ln : lines) {
        if (tracking != 0.0f && ln.end_idx > ln.begin_idx) {
            f32 line_tracking = 0.0f;
            for (size_t i = ln.begin_idx; i + 1 < ln.end_idx; ++i) {
                char_advances[i].advance += tracking;
                line_tracking += tracking;
            }
            ln.width += line_tracking;
        }
    }

    const f32 line_step = std::max(1.0f, font_size * line_height);

    if (box.y > 0.0f) {
        size_t max_lines = std::max(static_cast<size_t>(box.y / line_step), size_t{1});
        if (lines.size() > max_lines) lines.resize(max_lines);
    }
    if (lines.empty()) return result;

    f32 max_w = 0.0f;
    for (auto& ln : lines) max_w = std::max(max_w, ln.width);
    f32 total_h = static_cast<f32>(lines.size()) * line_step;
    result.total_width = max_w;
    result.total_height = total_h;

    f32 y_base = -total_h * 0.5f + line_step * 0.5f;
    for (size_t li = 0; li < lines.size(); ++li) {
        auto& ln = lines[li];
        f32 x = -ln.width * 0.5f;
        f32 y = y_base + static_cast<f32>(li) * line_step;
        for (size_t i = ln.begin_idx; i < ln.end_idx; ++i) {
            auto& ca = char_advances[i];
            TypewriterCharPos cp;
            cp.byte_offset = ca.byte_offset;
            cp.byte_len = ca.byte_len;
            cp.advance = ca.advance;
            cp.x = x + ca.advance * 0.5f;
            cp.y = y;
            result.chars.push_back(cp);
            x += ca.advance;
        }
    }

    // ── Populate out_placed (avoids double-shaping for callers who
    //     also need pre-shaped glyphs, e.g. typewriter_build). ───────
    // We use tracking=0 so the per-line tracking in the typewriter is
    // the single source of truth — the pre-shaped glyphs in out_placed
    // must NOT include resolver-level tracking, or glyph positions
    // would be inconsistent with the layout's raw_advance + per-line
    // tracking model.
    if (out_placed) {
        // run is still alive (captured in outer scope).
        *out_placed = resolve_placed_glyph_run(*run, 0.0f, text);
    }

    return result;
}

// ── typewriter_build — create per-character layers ───────────────────────
//
// Creates one layer per visible character.  Already-revealed characters are
// fully opaque and NEVER move.  Only the currently-revealing character fades.
// Layout is cached to avoid recomputing HarfBuzz shaping every frame.
//
// Uses the EXACT rendering pattern from the original working code:
// - l.pin_to(Anchor::Center) + l.opacity()
// - tp.anchor = TextAnchor::Center + PixelInk centering
// - tp.pos = {cp.x, cp.y, 0.0f} (position on TextParams, not layer)
// - tp.size = {font_size * 2, font_size * 2} (large box for ink overhang)
//
inline void typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame)
{
    FontSpec font_spec;
    font_spec.font_path = opts.font_path;
    font_spec.font_family = opts.font_family;
    font_spec.font_weight = opts.font_weight;

    // ── Thread-safe single-entry layout cache (frame-invariant inputs) ─
    //
    // A single static cache guarded by a mutex is sufficient because
    // typewriter_build() is typically called for short titles, not
    // paragraphs.  If per-composition isolation is ever needed the
    // cache can be lifted into a caller-supplied structure.
    //
    // Also caches the full PlacedGlyphRun so each character layer can
    // pass pre-shaped glyphs to the rasterizer (P0 fix for Arabic/Indic
    // contextual shaping).
    static std::mutex s_cache_mutex;
    static TypewriterLayout cached_layout;
    static PlacedGlyphRun cached_placed;  // full pre-shaped run
    static std::string cached_text;
    static f32         cached_font_size{0.0f};
    static f32         cached_tracking{0.0f};
    static Vec2        cached_box{0.0f, 0.0f};
    static f32         cached_line_height{0.0f};
    static FontSpec    cached_font_spec;

    std::lock_guard<std::mutex> lock(s_cache_mutex);

    bool cache_hit = (cached_text == opts.text &&
                      cached_font_size == opts.font_size &&
                      cached_tracking == opts.tracking &&
                      cached_box.x == opts.box.x &&
                      cached_box.y == opts.box.y &&
                      cached_line_height == opts.line_height &&
                      cached_font_spec.font_path == font_spec.font_path &&
                      cached_font_spec.font_family == font_spec.font_family &&
                      cached_font_spec.font_weight == font_spec.font_weight);

    if (!cache_hit) {
        // Recompute layout and get the PlacedGlyphRun via out_placed parameter
        // (avoids double-shaping: the layout function already shapes the text).
        cached_placed = PlacedGlyphRun{};
        cached_layout = compute_typewriter_layout(
            opts.text, opts.font_size, opts.tracking,
            opts.box, opts.line_height, font_spec,
            &cached_placed);  // populate cached_placed from the layout pass
        cached_text = opts.text;
        cached_font_size = opts.font_size;
        cached_tracking = opts.tracking;
        cached_box = opts.box;
        cached_line_height = opts.line_height;
        cached_font_spec = font_spec;
    }

    auto& layout = cached_layout;

    if (layout.chars.empty()) return;

    const f32 total_chars = static_cast<f32>(layout.chars.size());
    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(opts.start_delay);

    if (raw_frame < 0.0f) return;

    const f32 linear_t = std::clamp(raw_frame * opts.chars_per_frame / total_chars, 0.0f, 1.0f);
    const f32 eased_t = opts.easing.apply(linear_t);
    const f32 revealed_exact = eased_t * total_chars;
    const size_t revealed_count = static_cast<size_t>(revealed_exact);
    const f32 revealed_frac = revealed_exact - static_cast<f32>(revealed_count);

    // Build a pre-shaped mini run once and share it across layers via shared_ptr.
    // The shared_ptr ensures the data lives until all layers are rendered.
    // We build one run per revealed character, extracting just that character's
    // glyphs from the full cached_placed run.
    for (size_t i = 0; i < layout.chars.size(); ++i) {
        auto& cp = layout.chars[i];

        if (cp.byte_len == 1 && opts.text[cp.byte_offset] == ' ') continue;
        if (cp.byte_len == 1 && opts.text[cp.byte_offset] == '\n') continue;

        f32 opacity;
        if (i < revealed_count) {
            opacity = 1.0f;
        } else if (i == revealed_count) {
            opacity = opts.fade_easing.apply(revealed_frac);
        } else {
            break;
        }

        if (opacity < 0.005f) continue;

        // ── Extract this character's glyphs from the full placed run ──
        // Find which HarfBuzz clusters overlap this character's byte range.
        std::shared_ptr<PlacedGlyphRun> char_placed;
        if (!cached_placed.clusters.empty() && !cached_placed.glyphs.empty()) {
            const size_t char_start = cp.byte_offset;
            const size_t char_end = cp.byte_offset + cp.byte_len;

            // Find the first and last cluster index that overlap this range.
            size_t first_cl = cached_placed.clusters.size();
            size_t last_cl = 0;
            for (size_t ci = 0; ci < cached_placed.clusters.size(); ++ci) {
                const auto& cl = cached_placed.clusters[ci];
                const size_t cl_start = cl.byte_offset;
                const size_t cl_end = cl.byte_offset + cl.byte_len;
                // Overlap if ranges intersect
                if (cl_start < char_end && cl_end > char_start) {
                    if (ci < first_cl) first_cl = ci;
                    if (ci > last_cl) last_cl = ci;
                }
            }

            if (last_cl >= first_cl && first_cl < cached_placed.clusters.size()) {
                const auto& first_cluster = cached_placed.clusters[first_cl];
                const auto& last_cluster = cached_placed.clusters[last_cl];
                const size_t start_glyph = first_cluster.start_glyph;
                const size_t end_glyph = last_cluster.end_glyph;

                if (end_glyph > start_glyph && start_glyph < cached_placed.glyphs.size()) {
                    auto mini_run = std::make_shared<PlacedGlyphRun>();
                    mini_run->ascent = cached_placed.ascent;
                    mini_run->descent = cached_placed.descent;
                    mini_run->baseline = cached_placed.baseline;
                    mini_run->font_size = cached_placed.font_size;

                    // Copy the glyphs for this character, adjusting positions
                    // so the first glyph starts at (0, 0) relative offset.
                    const float base_x = cached_placed.glyphs[start_glyph].x;
                    const float base_y = cached_placed.glyphs[start_glyph].y;
                    for (size_t gi = start_glyph; gi < end_glyph; ++gi) {
                        const auto& src = cached_placed.glyphs[gi];
                        PlacedGlyph pg = src;
                        pg.x = src.x - base_x;
                        pg.y = src.y - base_y;
                        mini_run->glyphs.push_back(pg);
                        mini_run->total_width += src.advance_x;
                    }

                    // Build a single cluster for this mini run
                    PlacedGlyphRun::Cluster mini_cl;
                    mini_cl.start_glyph = 0;
                    mini_cl.end_glyph = mini_run->glyphs.size();
                    mini_cl.byte_offset = cp.byte_offset;
                    mini_cl.byte_len = cp.byte_len;
                    for (const auto& pg : mini_run->glyphs) {
                        mini_cl.advance += pg.advance_x;
                        mini_cl.raw_advance += pg.raw_advance_x;
                    }
                    mini_run->clusters.push_back(mini_cl);
                    mini_run->total_height = cached_placed.total_height;

                    char_placed = std::move(mini_run);
                }
            }
        }

        // For the text we still keep the substr — the rasterizer uses
        // pre_shaped glyphs (bypassing HarfBuzz), but the text is also
        // used for layout bounds / fallback.
        std::string glyph = opts.text.substr(cp.byte_offset, cp.byte_len);
        std::string lname = std::string(layer_prefix) + "_c" + std::to_string(i);

        s.layer(lname, [cp, glyph, opacity, char_placed,
                        fp = opts.font_path, ff = opts.font_family,
                        fw = opts.font_weight, fs = opts.font_size,
                        col = opts.color, lh = opts.line_height](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity(opacity);

            TextParams tp;
            tp.text = glyph;
            tp.size = {fs * 2.0f, fs * 2.0f};
            tp.pos = {cp.x, cp.y, 0.0f};
            tp.font_path = fp;
            tp.font_family = ff;
            tp.font_weight = fw;
            tp.font_size = fs;
            tp.color = col;
            tp.anchor = TextAnchor::Center;
            tp.centering_mode = TextCenteringMode::PixelInk;
            tp.align = TextAlign::Center;
            tp.vertical_align = VerticalAlign::Middle;
            tp.line_height = lh;
            tp.tracking = 0.0f;
            tp.wrap = TextWrap::None;
            tp.overflow = TextOverflow::Clip;
            tp.pre_shaped = char_placed;  // passes pre-shaped glyphs

            l.text("glyph", tp);
        });
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. glow_text
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

// ── compute_single_line_glyph_layout ─────────────────────────────────
//
// Convenience wrapper: calls compute_typewriter_layout with a huge box
// so wrapping never triggers.  Returns a stable, grapheme-cluster-safe
// per-character layout suitable for staggered typewriter animations.
//
// Example:
//   FontSpec spec{FONT_BOLD, "Poppins", 700};
//   auto layout = compute_single_line_glyph_layout(
//       phrase, font_size, tracking, spec);
//   for (auto& cp : layout.chars) {
//       std::string glyph = phrase.substr(cp.byte_offset, cp.byte_len);
//       f32 center_x = cp.x;
//   }
//
inline TypewriterLayout compute_single_line_glyph_layout(
    const std::string& text,
    f32 font_size,
    f32 tracking,
    const FontSpec& font)
{
    return compute_typewriter_layout(
        text,
        font_size,
        tracking,
        Vec2{100000.0f, font_size * 2.0f},
        1.0f,
        font
    );
}

} // namespace chronon3d::content::text