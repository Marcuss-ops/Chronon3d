#pragma once

#include <chronon3d/scene/shape.hpp>
#include <chronon3d/math/vec3.hpp>
#include <blend2d.h>
#include <optional>

namespace chronon3d {

struct TextRasterization {
    BLImage image;
    float x_offset;
    float y_offset;
    BLTextMetrics metrics;
    BLFont font;
};

std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& text,
    float effective_size,
    int padding = 4
);

} // namespace chronon3d
