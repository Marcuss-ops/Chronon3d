#pragma once

// ── Shared Text Helpers ─────────────────────────────────────────────────────
//
// Canonical text-building helpers used by ALL composition families.
//
// Namespace: chronon3d::content::text
//

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
// CenterTextOptions
// ═════════════════════════════════════════════════════════════════════════════
struct CenterTextOptions {
    std::string text;
    Vec2  box{1200.0f, 240.0f};
    Vec3  pos{0.0f, 0.0f, 0.0f};
    std::string font_path{"assets/fonts/Poppins-Bold.ttf"};
    std::string font_family{"Poppins"};
    int   font_weight{700};
    std::string font_style{"normal"};
    f32   font_size{96.0f};
    f32   tracking{0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    int   max_lines{1};
    bool  auto_fit{false};
    f32   line_height{0.95f};
    f32   min_font_size{12.0f};
    f32   max_font_size{160.0f};
};

// ═════════════════════════════════════════════════════════════════════════════
// 1. centered_text
// ═════════════════════════════════════════════════════════════════════════════
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
// 2. typewriter_build — stable per-character typewriter
// ═════════════════════════════════════════════════════════════════════════════
//
// Uses shape_text() for cluster mapping with max_advance calibration.
// Creates one layer per visible character with per-character opacity.
// Uses the EXACT rendering pattern from the original working code
// (tp.pos, TextAnchor::Center, PixelInk centering, large text box).
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

struct TypewriterCharPos {
    size_t byte_offset;
    size_t byte_len;
    f32 x;
    f32 y;
    f32 advance;
};

struct TypewriterLayout {
    std::vector<TypewriterCharPos> chars;
    f32 total_width{0.0f};
    f32 total_height{0.0f};
};

// ── Compute the full layout once ─────────────────────────────────────────
inline TypewriterLayout compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height,
    const FontSpec& font_spec)
{
    TypewriterLayout result;
    if (text.empty()) return result;

    auto& engine = shared_font_engine();

    auto run = engine.shape_text(text, font_spec, font_size);
    if (!run || run->glyphs.empty()) return result;

    // advance_x from shape_text is now pixel-accurate because
    // font_engine.cpp calls hb_ft_font_changed() after FT_Set_Pixel_Sizes.

    // Map glyph clusters → per-character advances (O(n))
    struct CharAdv {
        size_t byte_offset;
        size_t byte_len;
        f32 advance;
    };
    std::vector<CharAdv> char_advances;
    char_advances.reserve(run->glyphs.size());

    for (size_t gi = 0; gi < run->glyphs.size(); ++gi) {
        const auto& gp = run->glyphs[gi];
        size_t start_byte = static_cast<size_t>(gp.cluster);
        size_t end_byte = (gi + 1 < run->glyphs.size())
            ? static_cast<size_t>(run->glyphs[gi + 1].cluster)
            : text.size();

        CharAdv ca;
        ca.byte_offset = start_byte;
        ca.byte_len = end_byte - start_byte;
        ca.advance = gp.advance_x + tracking;
        char_advances.push_back(ca);
    }

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
    return result;
}

// ── typewriter_build — per-character layers with static opacity ───────────
//
// Uses the EXACT rendering pattern from the original working code:
// - l.pin_to(Anchor::Center) + l.opacity()
// - tp.anchor = TextAnchor::Center + PixelInk centering
// - tp.pos = {cp.x, cp.y, 0.0f} (position on TextParams, not layer)
// - tp.size = {font_size * 2, font_size * 2} (large box for ink overhang)
//
// Layout cache: compute_typewriter_layout is expensive (HarfBuzz shaping)
// and frame-invariant, so we cache the last computed layout. The cache is
// invalidated whenever any input (text, font, box, line_height) changes.
//
inline void typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame)
{
    FontSpec font_spec;
    font_spec.font_path = opts.font_path;
    font_spec.font_family = opts.font_family;
    font_spec.font_weight = opts.font_weight;

    // ── Single-entry layout cache (frame-invariant inputs) ──────────
    static TypewriterLayout cached_layout;
    static std::string cached_text;
    static f32         cached_font_size{0.0f};
    static f32         cached_tracking{0.0f};
    static Vec2        cached_box{0.0f, 0.0f};
    static f32         cached_line_height{0.0f};
    static FontSpec    cached_font_spec;

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
        cached_layout = compute_typewriter_layout(
            opts.text, opts.font_size, opts.tracking,
            opts.box, opts.line_height, font_spec);
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

        std::string glyph = opts.text.substr(cp.byte_offset, cp.byte_len);
        std::string lname = std::string(layer_prefix) + "_c" + std::to_string(i);

        s.layer(lname, [cp, glyph, opacity,
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

            l.text("glyph", tp);
        });
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. glow_text
// ═════════════════════════════════════════════════════════════════════════════
inline TextParams glow_text(CenterTextOptions o,
                            Color /*glow_color*/ = {1.0f, 1.0f, 1.0f, 1.0f},
                            f32 /*radius*/ = 24.0f,
                            f32 /*intensity*/ = 0.6f) {
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
