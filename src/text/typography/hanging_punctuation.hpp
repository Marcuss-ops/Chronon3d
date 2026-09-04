#pragma once

#include <chronon3d/text/composer_types.hpp>

#include "character_policy.hpp"

#include <algorithm>
#include <string_view>

namespace chronon3d::composer_internal {

[[nodiscard]] inline float compute_left_overhang(
    const ShapedCluster& cluster,
    std::string_view source_text,
    float hanging_limit
) {
    if (!cluster.punctuation || cluster.advance <= 0.0f) return 0.0f;
    if (source_text.empty() || cluster.source_byte_start >= source_text.size()) {
        return 0.0f;
    }

    const char32_t cp = decode_codepoint_at(
        source_text, cluster.source_byte_start);
    float fraction = 0.0f;
    switch (cp) {
    case U'"': case U'\u201C': case U'\u2018':
    case U'\u00AB': case U'\u2039':
        fraction = 0.45f; break;
    case U'.': case U',': case U';': case U':':
        fraction = 0.35f; break;
    case U'-': case U'\u2013': case U'\u2014':
        fraction = 0.25f; break;
    default:
        return 0.0f;
    }
    return std::min(fraction, hanging_limit) * cluster.advance;
}

[[nodiscard]] inline float compute_right_overhang(
    const ShapedCluster& cluster,
    std::string_view source_text,
    float hanging_limit
) {
    if (!cluster.punctuation || cluster.advance <= 0.0f) return 0.0f;
    if (source_text.empty() || cluster.source_byte_start >= source_text.size()) {
        return 0.0f;
    }

    const char32_t cp = decode_codepoint_at(
        source_text, cluster.source_byte_start);
    float fraction = 0.0f;
    switch (cp) {
    case U'"': case U'\u201D': case U'\u2019':
    case U'\u00BB': case U'\u203A':
        fraction = 0.45f; break;
    case U'.': case U',': case U';': case U':':
    case U'!': case U'?':
        fraction = 0.35f; break;
    case U'-': case U'\u2013': case U'\u2014':
        fraction = 0.25f; break;
    default:
        return 0.0f;
    }
    return std::min(fraction, hanging_limit) * cluster.advance;
}

} // namespace chronon3d::composer_internal
