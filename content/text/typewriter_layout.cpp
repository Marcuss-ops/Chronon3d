// ── Typewriter Layout ────────────────────────────────────────────────────────
// Implementation of compute_typewriter_layout and compute_single_line_glyph_layout.
// Extracted from text_helpers_typewriter.hpp.

#include "text_helpers_typewriter.hpp"
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/backends/text/text_unicode_utils.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chronon3d::content::text {

Result<TypewriterLayout, TextError> compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height,
    const FontSpec& font_spec,
    FontEngine& engine,
    PlacedGlyphRun* out_placed)
{
    TypewriterLayout result;
    if (text.empty()) return TextError{
        TextErrorCode::EmptyText,
        "compute_typewriter_layout: text is empty"};

    auto run = engine.shape_text(text, font_spec, font_size);
    if (!run || run->glyphs.empty()) return TextError{
        TextErrorCode::ShapingFailed,
        "compute_typewriter_layout: shaping produced no glyphs"};

    auto placed = resolve_placed_glyph_run(*run, tracking, text);
    if (placed.clusters.empty()) return TextError{
        TextErrorCode::NoClusters,
        "compute_typewriter_layout: placed clusters are empty"};

    struct CharAdv { size_t byte_offset; size_t byte_len; f32 advance; };
    std::vector<CharAdv> char_advances;
    char_advances.reserve(placed.clusters.size());
    for (const auto& cl : placed.clusters) {
        CharAdv ca;
        ca.byte_offset = cl.byte_offset;
        ca.byte_len = cl.byte_len;
        ca.advance = cl.raw_advance;
        char_advances.push_back(ca);
    }

    // Grapheme-cluster merge
    {
        std::vector<CharAdv> merged;
        merged.reserve(char_advances.size());

        enum class GB11State { Normal, ExtPict, ExtPictZWJ };
        GB11State gb11 = GB11State::Normal;

        for (size_t i = 0; i < char_advances.size(); ++i) {
            auto& ca = char_advances[i];

            size_t cp_offset = ca.byte_offset;
            if (cp_offset < text.size()) {
                const char32_t cp = chronon3d::detail::utf8_decode_cp(std::string_view(text), cp_offset);
                const bool is_ext = chronon3d::detail::is_grapheme_extend(cp);
                const bool is_ep  = chronon3d::detail::is_extended_pictographic(cp);
                const bool is_ri  = chronon3d::detail::is_regional_indicator(cp);
                const bool is_zwj = (cp == 0x200D);

                if (!merged.empty() && is_ri) {
                    size_t prev_off = merged.back().byte_offset;
                    if (prev_off < text.size()) {
                        const char32_t prev_cp = chronon3d::detail::utf8_decode_cp(
                            std::string_view(text), prev_off);
                        if (chronon3d::detail::is_regional_indicator(prev_cp)) {
                            auto& prev = merged.back();
                            prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                            prev.advance += ca.advance;
                            gb11 = GB11State::Normal;
                            continue;
                        }
                    }
                }

                if (is_zwj && gb11 == GB11State::ExtPict) {
                    gb11 = GB11State::ExtPictZWJ;
                    if (!merged.empty()) {
                        auto& prev = merged.back();
                        prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                        prev.advance += ca.advance;
                    }
                    continue;
                }

                if (is_ep && !is_ri && gb11 == GB11State::ExtPictZWJ) {
                    gb11 = GB11State::ExtPict;
                    if (!merged.empty()) {
                        auto& prev = merged.back();
                        prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                        prev.advance += ca.advance;
                    }
                    continue;
                }

                if (is_ext && !merged.empty()) {
                    auto& prev = merged.back();
                    prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                    prev.advance += ca.advance;
                    continue;
                }

                if (!is_ext) {
                    gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
                }
            }

            merged.push_back(ca);
        }

        char_advances = std::move(merged);
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

    // Apply tracking per line
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
    if (lines.empty()) return TextError{
        TextErrorCode::NoLayoutLines,
        "compute_typewriter_layout: word-wrap produced zero lines"};

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

    if (out_placed) {
        *out_placed = resolve_placed_glyph_run(*run, 0.0f, text);
    }

    return std::move(result);
}

Result<TypewriterLayout, TextError> compute_single_line_glyph_layout(
    const std::string& text,
    f32 font_size,
    f32 tracking,
    const FontSpec& font,
    FontEngine& engine)
{
    return compute_typewriter_layout(
        text,
        font_size,
        tracking,
        Vec2{100000.0f, font_size * 2.0f},
        1.0f,
        font,
        engine
    );
}

} // namespace chronon3d::content::text
