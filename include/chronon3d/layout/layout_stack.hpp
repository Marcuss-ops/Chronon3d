#pragma once

// ---------------------------------------------------------------------------
// layout_stack.hpp
//
// Horizontal and vertical stack layout primitives: hstack, vstack,
// BoxPadding, box_fit_content.
//
// Extracted from design_kit.hpp for single-responsibility.
// ---------------------------------------------------------------------------

#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace chronon3d {

struct LayoutElement {
    Vec2 size{0.0f, 0.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
};

[[nodiscard]] inline Vec2 hstack_size(const std::vector<Vec2>& sizes, f32 gap) {
    if (sizes.empty()) return {0.0f, 0.0f};
    f32 width = 0.0f;
    f32 height = 0.0f;
    for (size_t i = 0; i < sizes.size(); ++i) {
        width += sizes[i].x;
        height = std::max(height, sizes[i].y);
        if (i + 1 < sizes.size()) {
            width += gap;
        }
    }
    return {width, height};
}

[[nodiscard]] inline Vec2 vstack_size(const std::vector<Vec2>& sizes, f32 gap) {
    if (sizes.empty()) return {0.0f, 0.0f};
    f32 width = 0.0f;
    f32 height = 0.0f;
    for (size_t i = 0; i < sizes.size(); ++i) {
        width = std::max(width, sizes[i].x);
        height += sizes[i].y;
        if (i + 1 < sizes.size()) {
            height += gap;
        }
    }
    return {width, height};
}

[[nodiscard]] inline std::vector<LayoutElement> hstack(const std::vector<Vec2>& sizes, f32 gap, Vec3 start_pos = {0.0f, 0.0f, 0.0f}) {
    std::vector<LayoutElement> elements;
    elements.reserve(sizes.size());
    f32 current_x = start_pos.x;
    for (const auto& sz : sizes) {
        elements.push_back({
            .size = sz,
            .pos = {current_x + sz.x * 0.5f, start_pos.y + sz.y * 0.5f, start_pos.z}
        });
        current_x += sz.x + gap;
    }
    return elements;
}

[[nodiscard]] inline std::vector<LayoutElement> vstack(const std::vector<Vec2>& sizes, f32 gap, Vec3 start_pos = {0.0f, 0.0f, 0.0f}) {
    std::vector<LayoutElement> elements;
    elements.reserve(sizes.size());
    f32 current_y = start_pos.y;
    for (const auto& sz : sizes) {
        elements.push_back({
            .size = sz,
            .pos = {start_pos.x + sz.x * 0.5f, current_y + sz.y * 0.5f, start_pos.z}
        });
        current_y += sz.y + gap;
    }
    return elements;
}

struct BoxPadding {
    f32 left{0.0f};
    f32 top{0.0f};
    f32 right{0.0f};
    f32 bottom{0.0f};
};

[[nodiscard]] inline Vec2 box_fit_content(Vec2 content_size,
                                           BoxPadding padding = {},
                                           Vec2 min_size = {0.0f, 0.0f},
                                           std::optional<Vec2> max_size = std::nullopt) {
    Vec2 size{
        content_size.x + padding.left + padding.right,
        content_size.y + padding.top + padding.bottom
    };
    size.x = std::max(size.x, min_size.x);
    size.y = std::max(size.y, min_size.y);
    if (max_size) {
        size.x = std::min(size.x, max_size->x);
        size.y = std::min(size.y, max_size->y);
    }
    return size;
}

} // namespace chronon3d
