#pragma once

// ──────────────────────────────────────────────────────────────────────
// blend2d_path_stroker.hpp — PR5 single entry point for BL2-driven stroke.
//
// One function, called by `path_rasterizer.cpp::draw_path()` when
// `VectorRasterizerDebugMode::Blend2D` is active after the BL2 fill
// step.  Stroke caps / joins / anti-aliased outline coverage are
// produced by Blend2D; Chronon keeps the colour resolution and the
// framebuffer compositor.  Trim + dash are pre-applied by the caller
// (`prepare_stroke_contours`); each visible_subpath is shipped to
// BL2 as an open polyline with `BLStrokeOptions` only — no dash on
// the BL2 side, since Chronon's custom dash semantics differ.
//
// Cap semantics: BL2's butt/round/square honour LineCap directly.
// Join semantics: miter-with-limit (10°) / round / bevel; very sharp
// miter joins fall back to bevel via BL2's miter-limit policy.
// Gradient stroke (PathStroke.gradient) is APPLIED on the Chronon
// side per-pixel so the linear-float colour pipeline is preserved.
// ──────────────────────────────────────────────────────────────────────

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
// detail::PreparedStrokeContour lives in the sibling rasterizer/path
// subdir.  The CMakeLists in src/backends/software/blend2d/ does not
// add that subtree as an include directory, so we reference the
// header via a relative path matching the convention used for
// path_rasterizer.hpp in blend2d_path_filler.cpp.
#include "../rasterizers/path/path_stroke.hpp"  // detail::PreparedStrokeContour

namespace chronon3d::renderer {

/// Render the stroke portion of a `PathShape` using Blend2D's coverage
/// rasterizer, then shade and composite into `fb` inside `bbox`.
/// `stroke_color_modulation` is the per-primitive `Color` (RBG
/// modulates base stroke colour, A multiplies coverage alpha) — keeps
/// parity with the legacy per-pixel loop's RGB+alpha modulation.
/// `prepared` is the vector returned by
/// `prepare_stroke_contours(screen_contours, stroke)` (mutually
/// borrowed here to avoid recomputation).  `state->clip_rect` is NOT
/// applied here — the caller has already clipped the bbox.  Mask
/// integration is deferred to PR7 alongside fill masking.
/// Returns `false` when stroke is disabled, no prepared segments
/// exist, or bbox is empty/degenerate.
bool rasterize_path_stroke_blend2d(
    chronon3d::Framebuffer& fb,
    const chronon3d::PathShape& path,
    const chronon3d::Mat4& model,
    const chronon3d::Color& stroke_color_modulation,
    const chronon3d::renderer::detail::PreparedStrokeContour* prepared,
    std::size_t prepared_count,
    const chronon3d::raster::BBox& bbox,
    const RenderState* state);

} // namespace chronon3d::renderer
