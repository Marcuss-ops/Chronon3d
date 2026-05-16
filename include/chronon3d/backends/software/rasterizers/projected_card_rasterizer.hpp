#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

namespace chronon3d::renderer {

// Fill a ProjectedCard with a solid color using triangle rasterization.
// Splits the quad into TL→TR→BR and TL→BR→BL triangles.
void fill_projected_card(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Color& color
);

// Composite a source Framebuffer onto dst, warped to fit a ProjectedCard.
// Uses inverse-mapping: for each dst pixel inside the card bbox, samples src bilinearly.
// src_size is the original logical size of the source (for UV computation).
void composite_projected_framebuffer(
    Framebuffer& dst,
    const Framebuffer& src,
    const rendering::ProjectedCard& card,
    float opacity,
    BlendMode mode = BlendMode::Normal
);

} // namespace chronon3d::renderer
