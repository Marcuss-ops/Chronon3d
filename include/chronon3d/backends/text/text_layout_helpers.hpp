#pragma once

#include "text_layout_types.hpp"
#include "text_unicode_utils.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::detail::text_layout {

struct TextLayoutGeometry {
    float font_size{1.0f};
    float line_height{1.0f};
    float max_width{0.0f};
    bool wrapping_enabled{false};
};

[[nodiscard]] inline TextLayoutGeometry layout_geometry(const TextLayoutInput& input) {
    TextLayoutGeometry geometry;
    geometry.font_size = std::max(1.0f, input.style.size);
    geometry.line_height = std::max(1.0f, geometry.font_size * input.style.line_height);
    geometry.max_width = input.box.enabled && input.box.size.x > 0.0f ? input.box.size.x : 0.0f;
    geometry.wrapping_enabled = geometry.max_width > 0.0f && input.style.wrap != TextWrap::None;
    return geometry;
}

[[nodiscard]] inline float aligned_line_x(TextAlign align, float line_width, float available_width) {
    switch (align) {
    case TextAlign::Center:
        return (available_width - line_width) * 0.5f;
    case TextAlign::Right:
        return available_width - line_width;
    case TextAlign::Left:
    default:
        return 0.0f;
    }
}

inline void append_grapheme(std::string& destination, std::string_view source, size_t offset) {
    const size_t length = grapheme_byte_offset_at(source.substr(offset), 1);
    if (length != 0) destination.append(source.data() + offset, length);
}

[[nodiscard]] inline std::pair<float, float> fallback_font_metrics(float font_size) {
    return {font_size * 0.78f, font_size * 0.22f};
}

} // namespace chronon3d::detail::text_layout
