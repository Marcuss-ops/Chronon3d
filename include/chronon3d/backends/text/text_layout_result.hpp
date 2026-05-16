#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/scene/shape.hpp>

namespace chronon3d::text {

// Result of a text layout pass: total pixel width and initial cursor x-offset.
// Computed from font metrics + alignment before any rendering or projection.
struct TextLayoutResult {
    f32 total_width{0.0f};  // sum of all glyph advances in pixels
    f32 x_start{0.0f};     // initial cursor x: 0 for Left, -w/2 for Center, -w for Right
    f32 ascent{0.0f};       // ascent above baseline in pixels
    f32 descent{0.0f};      // descent below baseline (positive value)
    f32 line_height{0.0f};  // ascent + descent
};

// Compute x_start from total_width and alignment.
inline f32 layout_x_start(f32 total_width, TextAlign align) {
    switch (align) {
        case TextAlign::Center: return -total_width * 0.5f;
        case TextAlign::Right:  return -total_width;
        default:                return 0.0f;
    }
}

} // namespace chronon3d::text
