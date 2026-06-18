#pragma once

// ──────────────────────────────────────────────────────────────────────
// blend2d_path_filler.hpp — PR4 single entry point for BL2-driven fill.
//
// One function, called by `path_rasterizer.cpp::draw_path()` when
// `VectorRasterizerDebugMode::Blend2D` (or `Compare`) is active and
// the path has `fill.enabled == true`.  Owns the full A8 coverage
// rasterisation step AND the Chronon shade / compositor blend for
// the ROI rectangle, so the caller doesn't need to know about BL2.
//
// Stroke is intentionally NOT touched here — the existing custom
// stroke loop in `path_rasterizer.cpp` stays in charge until PR5.
// ──────────────────────────────────────────────────────────────────────

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

// RenderState lives in the chronon3d:: top-level namespace (it is
// transitively declared via <chronon3d/scene/model/render/render_node.hpp>
// which path_rasterizer.cpp already pulls in).  No local forward-decl
// is needed; the include above gives the full type, so callers do not
// need an extra include to satisfy this signature.

namespace chronon3d::renderer {

/// Render the fill portion of a `PathShape` using Blend2D's coverage
/// rasterizer, then shade and composite the result into `fb` inside
/// `bbox`.  `opacity` is the global alpha multiplier from the
/// primitive's `Color::a`.  Returns `false` on degenerate input
/// (empty path, empty bbox) so callers can fast-path to the legacy
/// loop without branching on the error code.
bool rasterize_path_fill_blend2d(
    chronon3d::Framebuffer& fb,
    const chronon3d::PathShape& path,
    const chronon3d::Mat4& model,
    chronon3d::f32 opacity,
    const chronon3d::raster::BBox& bbox,
    const RenderState* state);

} // namespace chronon3d::renderer
