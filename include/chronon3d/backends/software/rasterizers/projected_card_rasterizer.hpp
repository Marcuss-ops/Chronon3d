#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <span>

namespace chronon3d::renderer {

// Fill a ProjectedCard with a solid color using triangle rasterization.
// Splits the quad into TL→TR→BR and TL→BR→BL triangles.
// When depth_buffer is non-empty (w×h), performs per-pixel depth testing
// and writes interpolated Z to the depth buffer.
void fill_projected_card(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Color& color,
    std::span<float> depth_buffer = {}
);

// Composite a source Framebuffer onto dst, warped to fit a ProjectedCard.
// Uses inverse-mapping: for each dst pixel inside the card bbox, samples src bilinearly.
// When depth_buffer is non-empty (w×h), performs per-pixel depth testing.
void composite_projected_framebuffer(
    Framebuffer& dst,
    const Framebuffer& src,
    const rendering::ProjectedCard& card,
    float opacity,
    BlendMode mode = BlendMode::Normal,
    std::span<float> depth_buffer = {}
);

} // namespace chronon3d::renderer
