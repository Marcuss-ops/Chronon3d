#pragma once

// ============================================================================
// shape_stroke.hpp — Shared stroke infrastructure for shape primitives
//
// FASE 20 split: shape.hpp was monolithic (488L) and held all shape
// primitives + stroke infrastructure together.  This header owns the
// SHARED stroke types used by the 3 closed-shape primitives
// (Rect / RoundedRect / Circle).  LineShape has its own dedicated
// LineStroke (in `line_shape.hpp`) because its semantics diverge
// (trim_start / trim_end instead of full perimeter stroking).
//
// All extracted per-shape headers (rect_shape.hpp, rounded_rect_shape.hpp,
// circle_shape.hpp) include this file at file scope since their struct
// fields reference `ShapeStroke`.
//
// The umbrella shape.hpp re-exports all per-shape + stroke headers.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>

#include <optional>

namespace chronon3d {

enum class StrokeAlignment {
    Center,
    Inside,
    Outside,
};

struct ShapeStroke {
    bool enabled{false};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};

    /// Optional gradient fill.  When present, the stroke colours come
    /// from the gradient instead of the solid `color` field.
    std::optional<GradientFill> gradient;
};

} // namespace chronon3d
