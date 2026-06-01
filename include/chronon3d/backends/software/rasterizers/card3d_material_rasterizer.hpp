#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/scene/card3d_material.hpp>

namespace chronon3d::renderer {

// Render a ProjectedCard with Card3DMaterial (thickness, gradient, edge highlight, rim light).
// Renders in order: side faces → front gradient quad → edge highlight.
// The light direction from Card3DMaterial is used to determine which edges have side faces.
void render_card3d(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Card3DMaterial& material,
    float opacity = 1.0f
);

} // namespace chronon3d::renderer
