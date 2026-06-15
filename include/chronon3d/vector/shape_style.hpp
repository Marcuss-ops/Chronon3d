#pragma once

#include <chronon3d/graphics/shape_style/fill_style.hpp>

namespace chronon3d {

// Re-export the canonical types for code that is not yet migrated to
// chronon3d::graphics.  New code should prefer the graphics:: version.
using FillStyle    = graphics::FillStyle;
using StrokeStyle  = graphics::StrokeStyle;

// ── ShapeStyle — aggregates fill, stroke and opacity ────────────────
// Fill and stroke now use the canonical FillStyle / StrokeStyle types
// that reference chronon3d::graphics::GradientDefinition.

struct ShapeStyle {
    FillStyle   fill{FillStyle::solid({1.0f, 1.0f, 1.0f, 1.0f})};
    StrokeStyle stroke{};
    f32         opacity{1.0f};
};

} // namespace chronon3d
