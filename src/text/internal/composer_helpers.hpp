// ═══════════════════════════════════════════════════════════════════════════
// composer_helpers.hpp — Shared internals for paragraph composers
//
// Internal header — NOT part of the public API.  Included by both
// single_line_composer.cpp and every_line_composer.cpp.
//
// Contains: UTF-8 codepoint decoding, character classification,
// ShapedCluster construction, hanging punctuation, and justification.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/text/composer_types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/paragraph_style.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace chronon3d {
namespace composer_internal {

// ── UTF-8 codepoint decoding ──────────────────────────────────────────────

[[nodiscard]] inline char32_t decode_codepoint_at(std::string_view sv, size_t byte_start) {
    if (byte_start >= sv.size()) return 0xFFFD;
    unsigned char c = static_cast<unsigned char>(sv[byte_start]);

    if (c < 0x80) return static_cast<char32_t>(c);

    int len = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0)       { len = 2; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0)  { len = 3; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0)  { len = 4; cp = c & 0x07u; }
    else return 0xFFFD;

    if (byte_start + len > sv.size()) return 0xFFFD;

    for (int j = 1; j < len; ++j) {
        unsigned char cont = static_cast<unsigned char>(sv[byte_start + j]);
        if ((cont & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cont & 0x3Fu);
    }
    return cp;
}

// ── Character classification ─────────────────────────────────────────────

[[nodiscard]] inline bool is_whitespace_codepoint(char32_t cp) {
    switch (cp) {
    case U' ':  case U'\t':  case U'\r':  case U'\n':
    case 0x00A0: case 0x1680:
    case 0x2000: case 0x2001: case 0x2002: case 0x2003:
    case 0x2004: case 0x2005: case 0x2006: case 0x2007:
    case 0x2008: case 0x2009: case 0x200A:
    case 0x202F: case 0x205F: case 0x3000:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_mandatory_break_codepoint(char32_t cp) {
    return cp == U'\n' || cp == U'\r';
}

[[nodiscard]] inline bool is_punctuation_codepoint(char32_t cp) {
    switch (cp) {
    case U'.':  case U',':  case U';':  case U':':  case U'!':  case U'?':
    case U'"':  case U'\'': case U'`':
    case U'\u201C': case U'\u201D': case U'\u2018': case U'\u2019':
    case U'-':  case U'\u2013': case U'\u2014':
    case U'\u2026':
    case U'\u00AB': case U'\u00BB':
    case U'\u2039': case U'\u203A':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_soft_hyphen_at(std::string_view sv, size_t byte_start) {
    if (byte_start >= sv.size()) return false;
    unsigned char c1 = static_cast<unsigned char>(sv[byte_start]);
    if (c1 != 0xC2 || byte_start + 1 >= sv.size()) return false;
    return static_cast<unsigned char>(sv[byte_start + 1]) == 0xAD;
}

// ═══════════════════════════════════════════════════════════════════════════
// TEXT-PLY-01 — CJK kinsoku (simplified opening-bracket rule)
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] inline bool is_cjk_opening_bracket(char32_t cp) noexcept {
    switch (cp) {
    // CJK opening punctuation that should never end a line.
    case 0x300C:  // 「 LEFT CORNER BRACKET
    case 0x300E:  // 『 LEFT WHITE CORNER BRACKET
    case 0x300A:  // 《 LEFT DOUBLE ANGLE BRACKET
    case 0x3008:  // 〈 LEFT ANGLE BRACKET
    case 0xFF08:  // （ FULLWIDTH LEFT PARENTHESIS
    case 0x3010:  // 【 LEFT BLACK LENTICULAR BRACKET
    case 0xFF3B:  // ［ FULLWIDTH LEFT SQUARE BRACKET
    case 0x3014:  // 〔 LEFT TORTOISE SHELL BRACKET
        return true;
    default:
        return false;
    }
}

/// Walk clusters; when kinsoku is enabled, clear `allowed_break_after` on
/// CJK opening-bracket clusters so the line won't break immediately after
/// them.  Note: this implements only the simplified "no-break-after-opening"
/// rule.  The complementary "no-break-before-closing" rule would require
/// extending ShapedCluster with a `no_break_before` flag and is deferred
/// to a follow-up atom.
inline void apply_kinsoku(
    std::vector<ShapedCluster>& clusters,
    std::string_view source_text,
    const ParagraphStyle& style
) noexcept {
    if (!style.kinsoku) return;
    for (auto& cl : clusters) {
        if (cl.source_byte_start >= source_text.size()) continue;
        char32_t cp = decode_codepoint_at(source_text, cl.source_byte_start);
        if (is_cjk_opening_bracket(cp)) {
            cl.allowed_break_after = false;
        }
    }
}

// ── Build ShapedCluster vector from PlacedGlyphRun ──────────────────────

[[nodiscard]] inline std::vector<ShapedCluster> build_clusters(
    const PlacedGlyphRun& shaped,
    std::string_view source_text
) {
    std::vector<ShapedCluster> clusters;
    clusters.reserve(shaped.clusters.size());

    for (size_t i = 0; i < shaped.clusters.size(); ++i) {
        const auto& hb_cl = shaped.clusters[i];
        ShapedCluster sc;
        sc.source_byte_start = hb_cl.byte_offset;
        sc.source_byte_end   = hb_cl.byte_offset + hb_cl.byte_len;
        sc.first_glyph       = hb_cl.start_glyph;
        sc.glyph_count       = hb_cl.end_glyph - hb_cl.start_glyph;
        sc.advance           = hb_cl.advance;
        sc.ascent            = shaped.ascent;
        sc.descent           = shaped.descent;

        char32_t cp = decode_codepoint_at(source_text, sc.source_byte_start);
        sc.whitespace       = is_whitespace_codepoint(cp);
        sc.punctuation      = is_punctuation_codepoint(cp);
        sc.mandatory_break  = is_mandatory_break_codepoint(cp);
        sc.hyphenation_point = is_soft_hyphen_at(source_text, sc.source_byte_start);

        sc.allowed_break_after =
            sc.whitespace || sc.punctuation || sc.mandatory_break ||
            sc.hyphenation_point || (i + 1 == shaped.clusters.size());

        clusters.push_back(sc);
    }
    return clusters;
}

// ── Hanging punctuation ──────────────────────────────────────────────────

[[nodiscard]] inline float compute_left_overhang(
    const ShapedCluster& cluster,
    std::string_view source_text,
    float hanging_limit
) {
    if (!cluster.punctuation || cluster.advance <= 0.0f) return 0.0f;
    if (source_text.empty() || cluster.source_byte_start >= source_text.size())
        return 0.0f;

    char32_t cp = decode_codepoint_at(source_text, cluster.source_byte_start);
    float fraction = 0.0f;
    switch (cp) {
    case U'"':  case U'\u201C': case U'\u2018':
    case U'\u00AB': case U'\u2039':
        fraction = 0.45f; break;
    case U'.':  case U',':  case U';': case U':':
        fraction = 0.35f; break;
    case U'-':  case U'\u2013': case U'\u2014':
        fraction = 0.25f; break;
    default: return 0.0f;
    }
    return std::min(fraction, hanging_limit) * cluster.advance;
}

[[nodiscard]] inline float compute_right_overhang(
    const ShapedCluster& cluster,
    std::string_view source_text,
    float hanging_limit
) {
    if (!cluster.punctuation || cluster.advance <= 0.0f) return 0.0f;
    if (source_text.empty() || cluster.source_byte_start >= source_text.size())
        return 0.0f;

    char32_t cp = decode_codepoint_at(source_text, cluster.source_byte_start);
    float fraction = 0.0f;
    switch (cp) {
    case U'"':  case U'\u201D': case U'\u2019':
    case U'\u00BB': case U'\u203A':
        fraction = 0.45f; break;
    case U'.':  case U',':  case U';': case U':':
    case U'!':  case U'?':
        fraction = 0.35f; break;
    case U'-':  case U'\u2013': case U'\u2014':
        fraction = 0.25f; break;
    default: return 0.0f;
    }
    return std::min(fraction, hanging_limit) * cluster.advance;
}

// ── Justification ────────────────────────────────────────────────────────

inline void apply_justification(
    ComposedLine& line,
    float available_width,
    const ParagraphStyle& style,
    const std::vector<ShapedCluster>& clusters
) {
    float delta = available_width - line.natural_width;

    // TEXT-PLY-01: cap delta to ±justification_tolerance_px when configured
    // (>0).  Default 0.0 = back-compat (no clamp).  The clamp prevents weak
    // justification from spreading word/letter gaps beyond a sub-pixel
    // tolerance when the line shape is close to but not exactly at the
    // target width.
    const float tolerance = std::max(0.0f, style.justification_tolerance_px);
    if (tolerance > 0.0f) {
        delta = std::clamp(delta, -tolerance, tolerance);
    }

    switch (style.justification) {
    case TextJustification::Left:
        line.final_width = line.natural_width;
        line.glyph_scale = 1.0f;
        break;
    case TextJustification::Center:
        line.final_width = line.natural_width;
        line.glyph_scale = 1.0f;
        break;
    case TextJustification::Right:
        line.final_width = line.natural_width;
        line.glyph_scale = 1.0f;
        break;
    case TextJustification::Full:
    case TextJustification::FullLastLineLeft:
    case TextJustification::FullLastLineCenter:
    case TextJustification::FullLastLineRight: {
        line.final_width = available_width;
        if (delta == 0.0f || line.cluster_count <= 1) {
            line.glyph_scale = 1.0f;
            break;
        }
        int word_gaps = 0;
        for (size_t ci = line.first_cluster; ci + 1 < line.first_cluster + line.cluster_count; ++ci) {
            if (clusters[ci].whitespace) ++word_gaps;
        }
        float remaining = delta;
        const auto& sp = style.spacing;
        if (word_gaps > 0) {
            float per_gap = remaining / static_cast<float>(word_gaps);
            if (delta > 0) {
                per_gap = std::min(per_gap, sp.word_max - 1.0f);
            } else {
                per_gap = std::max(per_gap, -(1.0f - sp.word_min));
            }
            line.word_spacing_adjustment = per_gap;
            remaining -= per_gap * static_cast<float>(word_gaps);
        }
        if (std::abs(remaining) > 0.01f) {
            int letter_gaps = static_cast<int>(line.cluster_count) - 1;
            if (letter_gaps > 0) {
                float per_letter = remaining / static_cast<float>(letter_gaps);
                per_letter = std::clamp(per_letter, sp.letter_min, sp.letter_max);
                line.letter_spacing_adjustment = per_letter;
                remaining -= per_letter * static_cast<float>(letter_gaps);
            }
        }
        if (std::abs(remaining) > 0.01f && line.natural_width > 0.0f) {
            float scale = line.final_width / line.natural_width;
            scale = std::clamp(scale, sp.glyph_scale_min, sp.glyph_scale_max);
            line.glyph_scale = scale;
            line.final_width = line.natural_width * scale;
        }
        break;
    }
    }
}

/// Compute the natural width of a line from clusters [from, to),
/// excluding mandatory break clusters.
[[nodiscard]] inline float line_natural_width(
    const std::vector<ShapedCluster>& clusters,
    size_t from, size_t to
) {
    float w = 0.0f;
    for (size_t ci = from; ci < to; ++ci) {
        if (!clusters[ci].mandatory_break) w += clusters[ci].advance;
    }
    return w;
}

/// Count the number of word gaps (whitespace clusters not at line end)
/// in the range [from, to).
[[nodiscard]] inline int count_word_gaps(
    const std::vector<ShapedCluster>& clusters,
    size_t from, size_t to
) {
    int gaps = 0;
    for (size_t ci = from; ci + 1 < to; ++ci) {
        if (clusters[ci].whitespace) ++gaps;
    }
    return gaps;
}

/// Compute stretch capacity for a line (how much extra width word spacing
/// can absorb).  Returns pixels.
[[nodiscard]] inline float stretch_capacity(
    const std::vector<ShapedCluster>& clusters,
    size_t from, size_t to,
    const ParagraphSpacing& sp
) {
    int gaps = count_word_gaps(clusters, from, to);
    if (gaps == 0) return 0.0f;
    // Each gap can expand from 1.0× to word_max× of its natural spacing.
    // Natural word spacing = advance of whitespace clusters.
    float capacity = 0.0f;
    for (size_t ci = from; ci + 1 < to; ++ci) {
        if (clusters[ci].whitespace) {
            capacity += clusters[ci].advance * (sp.word_max - 1.0f);
        }
    }
    return capacity;
}

/// Compute shrink capacity for a line (how much width word spacing
/// can contract).  Returns positive pixels.
[[nodiscard]] inline float shrink_capacity(
    const std::vector<ShapedCluster>& clusters,
    size_t from, size_t to,
    const ParagraphSpacing& sp
) {
    int gaps = count_word_gaps(clusters, from, to);
    if (gaps == 0) return 0.0f;
    float capacity = 0.0f;
    for (size_t ci = from; ci + 1 < to; ++ci) {
        if (clusters[ci].whitespace) {
            capacity += clusters[ci].advance * (1.0f - sp.word_min);
        }
    }
    return capacity;
}

// ── Per-line post-processing (hanging punctuation, justification, baseline) ──
//
// Shared by both composers.  Takes the raw line breaks (first_cluster arrays)
// and fills in ComposedLine fields.

inline void finalize_lines(
    ParagraphLayout& result,
    const std::vector<ShapedCluster>& clusters,
    float available_width,
    const ParagraphStyle& style,
    std::string_view source_text,
    const PlacedGlyphRun& shaped
) {
    float max_line_width = 0.0f;
    float cumulative_height = style.space_before;
    // TEXT-PLY-01: tight_leading multiplier [0.0, 1.0) reduces effective
    // line height.  -1.0 sentinel = inherit (no modification).
    const float leading_scale = (style.tight_leading >= 0.0f && style.tight_leading < 1.0f)
        ? style.tight_leading : 1.0f;

    for (size_t li = 0; li < result.lines.size(); ++li) {
        auto& line = result.lines[li];
        const bool is_last_line = (li + 1 == result.lines.size());

        // Compute natural width if not already set
        if (line.natural_width == 0.0f && line.cluster_count > 0) {
            line.natural_width = line_natural_width(clusters, line.first_cluster,
                                                     line.first_cluster + line.cluster_count);
        }

        float line_available = available_width;
        if (li == 0) {
            line_available -= style.first_line_indent;
            if (line_available < 1.0f) line_available = 1.0f;
        }

        // Hanging punctuation
        float left_overhang = 0.0f;
        float right_overhang = 0.0f;
        if (style.hanging_punctuation && line.cluster_count > 0) {
            const auto& first_cl = clusters[line.first_cluster];
            const auto& last_cl  = clusters[line.first_cluster + line.cluster_count - 1];
            left_overhang  = compute_left_overhang(first_cl, source_text, style.hanging_limit);
            right_overhang = compute_right_overhang(last_cl, source_text, style.hanging_limit);
            line.left_overhang = left_overhang;
            line.right_overhang = right_overhang;
        }

        // Justification with last-line remapping
        ParagraphStyle line_style = style;
        if (is_last_line) {
            switch (style.justification) {
            case TextJustification::Full:                line_style.justification = TextJustification::Left;   break;
            case TextJustification::FullLastLineLeft:    line_style.justification = TextJustification::Left;   break;
            case TextJustification::FullLastLineCenter:  line_style.justification = TextJustification::Center; break;
            case TextJustification::FullLastLineRight:   line_style.justification = TextJustification::Right;  break;
            default: break;
            }
        }
        apply_justification(line, line_available, line_style, clusters);

        // Baseline Y
        if (line.cluster_count > 0) {
            line.baseline_y = cumulative_height + clusters[line.first_cluster].ascent;
        } else {
            line.baseline_y = cumulative_height;
        }

        // Line height (TEXT-PLY-01: tight_leading multiplier applied)
        float line_height = 0.0f;
        for (size_t ci = line.first_cluster; ci < line.first_cluster + line.cluster_count; ++ci) {
            if (!clusters[ci].mandatory_break) {
                float h = clusters[ci].ascent + clusters[ci].descent;
                if (h > line_height) line_height = h;
            }
        }
        if (line_height <= 0.0f) line_height = shaped.ascent + shaped.descent;
        line_height *= leading_scale;
        cumulative_height += line_height;

        float effective_width = line.final_width + left_overhang + right_overhang;
        if (effective_width > max_line_width) max_line_width = effective_width;
    }

    cumulative_height += style.space_after;
    result.bounds = Vec2{max_line_width, cumulative_height};
}

} // namespace composer_internal
} // namespace chronon3d
