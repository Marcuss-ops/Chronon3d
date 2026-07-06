#pragma once

// ============================================================================
// line_shape.hpp — LineStroke + LineShape primitive payload
//
// FASE 20 split: extracted from shape.hpp (488L).  Line has its own
// dedicated `LineStroke` (with `trim_start` / `trim_end` for path
// trimming) rather than reusing `ShapeStroke`, because line-trim
// semantics differ from closed-shape perimeter stroking.  The trim
// values are normalised [0,1] along the line.
//
// Uses `StrokeAlignment` from the shared shape_stroke.hpp.
// Consumed by `ShapePayload` variant index 4 (ShapeType::Line).
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/shape/shape_stroke.hpp>  // for StrokeAlignment

namespace chronon3d {

// ── LineStroke — line-only stroke with trim semantics ───────────────────
// Defines the visible portion of a line via normalised trim values.
//   trim_start = 0 → full line from `to=0`
//   trim_end   = 1 → full line to `to`
// The default `enabled=true` aligns with closed-shape stroke semantics.
struct LineStroke {
    f32 trim_start{0.0f};  // normalised [0..1]
    f32 trim_end{1.0f};
    bool enabled{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};
};

struct LineShape {
    Vec3 to{0.0f, 0.0f, 0.0f};
    f32 thickness{1.0f};
    LineStroke stroke{};
};

} // namespace chronon3d
