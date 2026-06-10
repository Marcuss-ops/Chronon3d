#pragma once

// ---------------------------------------------------------------------------
// diagnostics/internal/helpers.hpp
//
// Shared private helpers used by the bbox / layout / nulls overlays.
// Not part of the public diagnostics API — implementation details.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <string>

namespace chronon3d::renderer::diagnostics::internal {

/// Draw a + shaped crosshair (two orthogonal lines of given half-length).
void draw_crosshair(Framebuffer& fb, Vec2 center, f32 radius, const Color& color);

/// Best-effort enum-name lookup. Returns "Unknown" when not registered.
const char* shape_type_name(ShapeType type);

} // namespace chronon3d::renderer::diagnostics::internal
