#pragma once

// ──────────────────────────────────────────────────────────────────────
// blend2d_path_builder.hpp — PR4 PathShape → BLPath conversion
//
// Converts a Chronon3d `PathShape` (MoveTo / LineTo / QuadraticTo /
// CubicTo / Close) into a Blend2D `BLPath` that lives in *screen*
// space relative to the ROI bbox origin.  This decouples the
// renderer-side affine transform from the Blend2D rasterizer, which
// only needs the path in screen units over its target coverage
// surface.
//
// The conversion is intentionally minimal: control points are
// individually projected via `model * Vec4(x, y, 0, 1)` and offset
// by `-bbox_origin` so the BLPath origin sits at (0, 0) of the ROI.
// No flattening is performed; the higher-order Bezier segments are
// passed through to BL2 unchanged.  That makes the builder O(N)
// in command count and avoids the per-frame re-flatten cost that the
// custom path renderer paid in `flatten_path_geometry_cached`.
//
// Lives next to `blend2d_paint.{hpp,cpp}` so future PR6 Projective
// path support picks up the same conversion entry-point with only a
// different transform function (`project_point` instead of
// `transform_point`).
// ──────────────────────────────────────────────────────────────────────

#include <blend2d.h>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

namespace chronon3d::blend2d_bridge::path_builder {

/// Build a `BLPath` representing `path` in screen space, with the
/// bbox origin translated to (0, 0).  Each command's geometry points
/// are mapped by `transform_point(model, .)` so a caller-supplied
/// Mat4 already accounts for any 2.5D / camera-driven projection
/// performed in `path_rasterizer.cpp`.  The resulting path is ready
/// to be handed to `BLContext::fillPath` against a CoverageSurface
/// the size of `(roi_w × roi_h)`.
[[nodiscard]] BLPath make_bl_path(const chronon3d::PathShape& path,
                                  const chronon3d::Mat4& model,
                                  chronon3d::Vec2 bbox_origin);

} // namespace chronon3d::blend2d_bridge::path_builder
