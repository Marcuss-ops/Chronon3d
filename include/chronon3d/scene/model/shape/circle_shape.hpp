#pragma once

// ============================================================================
// circle_shape.hpp — CircleShape primitive payload
//
// FASE 20 split: extracted from shape.hpp (488L).  Uses the shared
// `ShapeStroke` from shape_stroke.hpp.  Consumed by `ShapePayload`
// variant index 3 (ShapeType::Circle).
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape_stroke.hpp>

namespace chronon3d {

struct CircleShape {
    f32 radius{50.0f};
    ShapeStroke stroke{};
};

} // namespace chronon3d
