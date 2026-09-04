#pragma once

#include <chronon3d/text/composer_types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/paragraph_style.hpp>

#include "../boundary_resolver/text_boundary_resolver.hpp"
#include "character_policy.hpp"

#include <string_view>
#include <vector>

namespace chronon3d::composer_internal {

[[nodiscard]] inline bool is_cjk_opening_bracket(char32_t cp) noexcept {
    switch (cp) {
    case 0x300C: // 「
    case 0x300E: // 『
    case 0x300A: // 《
    case 0x3008: // 〈
    case 0xFF08: // （
    case 0x3010: // 【
    case 0xFF3B: // ［
    case 0x3014: // 〔
        return true;
    default:
        return false;
    }
}

/// Chronon typography policy layered on top of ICU line opportunities.
inline void apply_kinsoku(
    std::vector<ShapedCluster>& clusters,
    std::string_view source_text,
    const ParagraphStyle& style
) noexcept {
    if (!style.kinsoku) return;
    for (auto& cluster : clusters) {
        if (cluster.source_byte_start >= source_text.size()) continue;
        if (is_cjk_opening_bracket(
                decode_codepoint_at(source_text, cluster.source_byte_start))) {
            cluster.allowed_break_after = false;
        }
    }
}

[[nodiscard]] inline std::vector<ShapedCluster> build_clusters(
    const PlacedGlyphRun& shaped,
    std::string_view source_text,
    const ParagraphStyle& style
) {
    std::vector<ShapedCluster> clusters;
    clusters.reserve(shaped.clusters.size());

    text::boundary::IcuBoundaryResolver boundary_resolver;
    const auto boundary_map = boundary_resolver.resolve(
        source_text,
        text::boundary::TextBoundaryOptions{style.language});

    for (std::size_t i = 0; i < shaped.clusters.size(); ++i) {
        const auto& hb_cluster = shaped.clusters[i];
        ShapedCluster cluster;
        cluster.source_byte_start = hb_cluster.byte_offset;
        cluster.source_byte_end = hb_cluster.byte_offset + hb_cluster.byte_len;
        cluster.first_glyph = hb_cluster.start_glyph;
        cluster.glyph_count = hb_cluster.end_glyph - hb_cluster.start_glyph;
        cluster.advance = hb_cluster.advance;
        cluster.ascent = shaped.ascent;
        cluster.descent = shaped.descent;

        const char32_t cp = decode_codepoint_at(
            source_text, cluster.source_byte_start);
        cluster.whitespace = is_whitespace_codepoint(cp);
        cluster.punctuation = is_punctuation_codepoint(cp);
        cluster.mandatory_break = is_mandatory_break_codepoint(cp);
        cluster.hyphenation_point = is_soft_hyphen_at(
            source_text, cluster.source_byte_start);

        // ICU owns standard Unicode line opportunities. Chronon only adds
        // engine semantics: mandatory breaks, soft hyphens, end-of-run and
        // policy transforms such as kinsoku.
        cluster.allowed_break_after =
            cluster.mandatory_break || cluster.hyphenation_point ||
            (i + 1 == shaped.clusters.size()) ||
            boundary_map.is_line_break(cluster.source_byte_end);

        clusters.push_back(cluster);
    }
    return clusters;
}

} // namespace chronon3d::composer_internal
