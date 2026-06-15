#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <optional>
#include <span>

namespace chronon3d::renderer {

// Render a ProjectedCard with Card3DMaterial (thickness, gradient, edge highlight, rim light).
// Renders in order: side faces → front gradient quad → edge highlight.
// The light direction from Card3DMaterial is used to determine which edges have side faces.
// When depth_buffer is non-empty (w×h), performs per-pixel depth testing.
void render_card3d(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Card3DMaterial& material,
    float opacity = 1.0f,
    std::span<float> depth_buffer = {}
);

struct Card3DRenderParams {
    Vec2 position{0.0f, 0.0f};     // top-left of the front face (pixels)
    Vec2 size{200.0f, 120.0f};     // width, height of the front face (pixels)
};

/// Render a Card3DMaterial onto a framebuffer using local pixel coordinates.
///
/// Draws:
///   1. Right side quad (extruded by thickness_px)
///   2. Bottom side quad (extruded by thickness_px)
///   3. Front face with gradient (front_top → front_bottom)
///   4. Edge highlight along the extruded edges
///   5. Rim light overlay if lit_context is provided
///
/// When depth_buffer is non-empty (w×h), performs per-pixel depth testing.
/// The card is rendered in local pixel coordinates; the caller is
/// responsible for world transform / projection.
void render_card3d_material(
    Framebuffer& fb,
    const Card3DMaterial& material,
    const Card3DRenderParams& params,
    const std::optional<rendering::LightContext>& lit_context = std::nullopt,
    std::span<float> depth_buffer = {}
);

} // namespace chronon3d::renderer
