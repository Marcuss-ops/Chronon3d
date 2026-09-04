#pragma once

#include <chronon3d/text/composer_types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/paragraph_style.hpp>

#include "hanging_punctuation.hpp"
#include "justification_policy.hpp"
#include "line_break_policy.hpp"

#include <string_view>
#include <vector>

namespace chronon3d::composer_internal {

inline void finalize_lines(
    ParagraphLayout& result,
    const std::vector<ShapedCluster>& clusters,
    float available_width,
    const ParagraphStyle& style,
    std::string_view source_text,
    const PlacedGlyphRun& shaped
) {
    if (style.max_lines > 0 &&
        result.lines.size() > static_cast<std::size_t>(style.max_lines)) {
        result.lines.resize(static_cast<std::size_t>(style.max_lines));
        result.truncated = true;
        result.rendered_ellipsis = style.ellipsis;
    }

    float max_line_width = 0.0f;
    float cumulative_height = style.space_before;
    const float leading_scale =
        (style.tight_leading >= 0.0f && style.tight_leading < 1.0f)
            ? style.tight_leading
            : 1.0f;

    for (std::size_t li = 0; li < result.lines.size(); ++li) {
        auto& line = result.lines[li];
        const bool is_last_line = li + 1 == result.lines.size();

        if (line.natural_width == 0.0f && line.cluster_count > 0) {
            line.natural_width = line_natural_width(
                clusters, line.first_cluster,
                line.first_cluster + line.cluster_count);
        }

        float line_available = available_width;
        if (li == 0) {
            line_available -= style.first_line_indent;
            if (line_available < 1.0f) line_available = 1.0f;
        }

        float left_overhang = 0.0f;
        float right_overhang = 0.0f;
        if (style.hanging_punctuation && line.cluster_count > 0) {
            const auto& first_cluster = clusters[line.first_cluster];
            const auto& last_cluster =
                clusters[line.first_cluster + line.cluster_count - 1];
            left_overhang = compute_left_overhang(
                first_cluster, source_text, style.hanging_limit);
            right_overhang = compute_right_overhang(
                last_cluster, source_text, style.hanging_limit);
            line.left_overhang = left_overhang;
            line.right_overhang = right_overhang;
        }

        ParagraphStyle line_style = style;
        if (is_last_line) {
            switch (style.justification) {
            case TextJustification::Full:
            case TextJustification::FullLastLineLeft:
                line_style.justification = TextJustification::Left;
                break;
            case TextJustification::FullLastLineCenter:
                line_style.justification = TextJustification::Center;
                break;
            case TextJustification::FullLastLineRight:
                line_style.justification = TextJustification::Right;
                break;
            default:
                break;
            }
        }
        apply_justification(line, line_available, line_style, clusters);

        switch (line_style.justification) {
        case TextJustification::Center:
        case TextJustification::FullLastLineCenter:
            line.alignment_offset =
                (line_available - line.final_width) * 0.5f;
            break;
        case TextJustification::Right:
        case TextJustification::FullLastLineRight:
            line.alignment_offset = line_available - line.final_width;
            break;
        default:
            line.alignment_offset = 0.0f;
            break;
        }

        line.baseline_y = line.cluster_count > 0
            ? cumulative_height + clusters[line.first_cluster].ascent
            : cumulative_height;

        float line_height = 0.0f;
        for (std::size_t ci = line.first_cluster;
             ci < line.first_cluster + line.cluster_count; ++ci) {
            if (!clusters[ci].mandatory_break) {
                const float height = clusters[ci].ascent + clusters[ci].descent;
                if (height > line_height) line_height = height;
            }
        }
        if (line_height <= 0.0f) {
            line_height = shaped.ascent + shaped.descent;
        }
        line_height *= leading_scale;
        cumulative_height += line_height;

        const float effective_width =
            line.final_width + left_overhang + right_overhang;
        if (effective_width > max_line_width) max_line_width = effective_width;
    }

    cumulative_height += style.space_after;
    result.bounds = Vec2{max_line_width, cumulative_height};
}

} // namespace chronon3d::composer_internal
