#pragma once

#include <chronon3d/text/composer_types.hpp>
#include <chronon3d/text/paragraph_style.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::composer_internal {

inline void apply_justification(
    ComposedLine& line,
    float available_width,
    const ParagraphStyle& style,
    const std::vector<ShapedCluster>& clusters
) {
    float delta = available_width - line.natural_width;
    const float tolerance = std::max(0.0f, style.justification_tolerance_px);
    if (tolerance > 0.0f) {
        delta = std::clamp(delta, -tolerance, tolerance);
    }

    switch (style.justification) {
    case TextJustification::Left:
    case TextJustification::Center:
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
        for (std::size_t ci = line.first_cluster;
             ci + 1 < line.first_cluster + line.cluster_count; ++ci) {
            if (clusters[ci].whitespace) ++word_gaps;
        }
        float remaining = delta;
        const auto& spacing = style.spacing;
        if (word_gaps > 0) {
            float per_gap = remaining / static_cast<float>(word_gaps);
            if (delta > 0.0f) {
                per_gap = std::min(per_gap, spacing.word_max - 1.0f);
            } else {
                per_gap = std::max(per_gap, -(1.0f - spacing.word_min));
            }
            line.word_spacing_adjustment = per_gap;
            remaining -= per_gap * static_cast<float>(word_gaps);
        }
        if (std::abs(remaining) > 0.01f) {
            const int letter_gaps = static_cast<int>(line.cluster_count) - 1;
            if (letter_gaps > 0) {
                float per_letter = remaining / static_cast<float>(letter_gaps);
                per_letter = std::clamp(
                    per_letter, spacing.letter_min, spacing.letter_max);
                line.letter_spacing_adjustment = per_letter;
                remaining -= per_letter * static_cast<float>(letter_gaps);
            }
        }
        if (std::abs(remaining) > 0.01f && line.natural_width > 0.0f) {
            float scale = line.final_width / line.natural_width;
            scale = std::clamp(
                scale, spacing.glyph_scale_min, spacing.glyph_scale_max);
            line.glyph_scale = scale;
            line.final_width = line.natural_width * scale;
        }
        break;
    }
    }
}

[[nodiscard]] inline float line_natural_width(
    const std::vector<ShapedCluster>& clusters,
    std::size_t from,
    std::size_t to
) {
    float width = 0.0f;
    for (std::size_t ci = from; ci < to; ++ci) {
        if (!clusters[ci].mandatory_break) width += clusters[ci].advance;
    }
    return width;
}

[[nodiscard]] inline int count_word_gaps(
    const std::vector<ShapedCluster>& clusters,
    std::size_t from,
    std::size_t to
) {
    int gaps = 0;
    for (std::size_t ci = from; ci + 1 < to; ++ci) {
        if (clusters[ci].whitespace) ++gaps;
    }
    return gaps;
}

[[nodiscard]] inline float stretch_capacity(
    const std::vector<ShapedCluster>& clusters,
    std::size_t from,
    std::size_t to,
    const ParagraphSpacing& spacing
) {
    if (count_word_gaps(clusters, from, to) == 0) return 0.0f;
    float capacity = 0.0f;
    for (std::size_t ci = from; ci + 1 < to; ++ci) {
        if (clusters[ci].whitespace) {
            capacity += clusters[ci].advance * (spacing.word_max - 1.0f);
        }
    }
    return capacity;
}

[[nodiscard]] inline float shrink_capacity(
    const std::vector<ShapedCluster>& clusters,
    std::size_t from,
    std::size_t to,
    const ParagraphSpacing& spacing
) {
    if (count_word_gaps(clusters, from, to) == 0) return 0.0f;
    float capacity = 0.0f;
    for (std::size_t ci = from; ci + 1 < to; ++ci) {
        if (clusters[ci].whitespace) {
            capacity += clusters[ci].advance * (1.0f - spacing.word_min);
        }
    }
    return capacity;
}

} // namespace chronon3d::composer_internal
