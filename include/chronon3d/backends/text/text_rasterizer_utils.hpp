#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/math/glm_types.hpp>
#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif
#include <cstdint>

namespace chronon3d {

#ifdef CHRONON3D_USE_BLEND2D

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
