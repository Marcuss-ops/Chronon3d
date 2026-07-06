#pragma once

// ============================================================================
// rect_shape.hpp — RectShape primitive payload
//
// FASE 20 split: extracted from shape.hpp (488L) into a single-purpose
// header.  Uses the shared `ShapeStroke` + `StrokeAlignment` from
// `shape_stroke.hpp`.  Consumed by `ShapePayload` variant as
// std::variant index 1 (ShapeType::Rect).
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape_stroke.hpp>

namespace chronon3d {

struct RectShape {
    Vec2 size{100.0f, 100.0f};
    ShapeStroke stroke{};
};

} // namespace chronon3d
