#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/math/glm_types.hpp>
#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif
#include <cstdint>

namespace chronon3d {

// Forward declaration — full definition is gated by CHRONON3D_USE_BLEND2D,
// but TextRenderResources references std::shared_ptr<TextRasterization>
// unconditionally in its public API.
struct TextRasterization;

#ifdef CHRONON3D_USE_BLEND2D

/// Result of a text-rasterization pass.  Kept lightweight so it can be
/// cached by TextRenderResources::raster_cache as a shared_ptr.
struct TextRasterization {
    BLImage image;
    float x_offset{0.0f};
    float y_offset{0.0f};
    BLTextMetrics metrics{};
    BLFont font{};
};

/// Apply TextMaterial effects (gradient, bevel, highlight, shade, emissive)
/// to a rasterized text BLImage in-place.
void apply_text_material(BLImage& img, const TextMaterial& mat);

#endif // CHRONON3D_USE_BLEND2D

uint64_t hash_text_style(
    const TextShape& t,
    float effective_size,
    int padding,
    const Mat4* transform = nullptr
);

} // namespace chronon3d
