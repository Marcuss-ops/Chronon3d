#pragma once

// ============================================================================
// rounded_rect_shape.hpp — RoundedRectShape primitive payload
//
// FASE 20 split: extracted from shape.hpp (488L).  Corners follow a
// circular arc of the given radius; radius is clamped to min(width,height)
// / 2 at render time.  Uses the shared `ShapeStroke` from shape_stroke.hpp.
// Consumed by `ShapePayload` variant index 2 (ShapeType::RoundedRect).
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape_stroke.hpp>

namespace chronon3d {

struct RoundedRectShape {
    Vec2 size{100.0f, 100.0f};
    f32 radius{8.0f};
    ShapeStroke stroke{};
};

} // namespace chronon3d
