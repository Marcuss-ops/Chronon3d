#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  LayoutResolver — flex-style solver for motion_studio UiNode trees.
//
//  Intentionally *not* the engine's existing `chronon3d::layout::LayoutSolver`
//  (which works on already-built `Layer`s).  This resolver operates on the
//  intermediate UiNode DOM emitted by UiBuilder, before any layers exist.
//
//  Two-pass flex algorithm (sufficient for SaaS composition):
//    1. compute_intrinsic_size  — bottom-up: leaves use declared Fixed size;
//       containers sum children + gap + padding (FitContent) or stretch
//       (FillRemaining).
//    2. layout                  — top-down: parent places children using main
//       axis (direction + justify) and cross axis (align) given its resolved
//       inner box (parent box minus padding).
//
//  Grid: items flow left → right, top → bottom in row-major order across
//  `grid_cols` columns.  Gap is uniform.  Each row's height is the max of
//  its items; the grid's total height is the sum of row heights + gaps.
//
//  Percent: not implemented in the slice (keep the algorithm clean).
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/motion_studio/layout/ui_node.hpp>

namespace chronon3d::motion_studio {

class LayoutResolver {
public:
    /// Solve the entire tree rooted at `root` against the given canvas size.
    /// Writes `resolved_position` (top-left in canvas pixels) and
    /// `resolved_size` onto every node in the tree.
    void solve(UiNode& root, Vec2 canvas_size);

private:
    Vec2 compute_intrinsic_size(const UiNode& node) const;
    void layout_node(UiNode& node);

    Vec2 canvas_size_{0.0f, 0.0f};
};

} // namespace chronon3d::motion_studio
