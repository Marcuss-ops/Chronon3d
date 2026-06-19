#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <blend2d.h>
#include <optional>

namespace chronon3d {

class FontEngine;  // forward declaration

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
    int padding = 4,
    bool* cache_hit = nullptr,
    const Mat4* transform = nullptr,
    FontEngine* font_engine = nullptr
);

/// Inject the text raster cache capacity at startup (called once by
/// SoftwareRenderer).  Must be called before first rasterize_text_to_bl_image().
void set_text_cache_capacity(size_t max_bytes);

void clear_text_raster_cache();

uint64_t hash_text_style(
    const TextShape& t,
    float effective_size,
    int padding,
    const Mat4* transform = nullptr
);

/// Apply TextMaterial effects (gradient, bevel, highlight, shade, emissive)
/// to a rasterized text BLImage in-place.
void apply_text_material(BLImage& img, const TextMaterial& mat);

} // namespace chronon3d
