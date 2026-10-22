#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <span>

namespace chronon3d::renderer {


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
