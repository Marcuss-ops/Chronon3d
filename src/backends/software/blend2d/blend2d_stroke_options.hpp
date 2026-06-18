#pragma once

// ──────────────────────────────────────────────────────────────────────
// blend2d_stroke_options.hpp — PR5 PathStroke → BL2 stroke-state bridge
//
// Maps the Chronon3d PathStroke knobs (cap / join / width / miter limit)
// onto the BL2 vcpkg-installed `BLContext` setters.  This vcpkg fork
// does NOT expose `BLStrokeOptions::caps` / `::joins` / `::dashLength`
// as public members — only `width` / `miterLimit` / `dashOffset` /
// `dashArray` (inside the C-only `BLStrokeOptionsCore`), so we apply
// knobs via the run-time setters rather than a struct.
//
// Cap / Join mapping:
//
//   LineCap::Butt   → BL_STROKE_CAP_BUTT
//   LineCap::Round  → BL_STROKE_CAP_ROUND
//   LineCap::Square → BL_STROKE_CAP_SQUARE
//
//   LineJoin::Miter → BL_STROKE_JOIN_MITER  (miterLimit = 10°)
//   LineJoin::Round → BL_STROKE_JOIN_ROUND
//   LineJoin::Bevel → BL_STROKE_JOIN_BEVEL
//
// Width: passthrough into `BLContext::setStrokeWidth` (BL2 expects
// pixels).  Dash: Chronon pre-computes via `prepare_stroke_contours`,
// so we leave BL2's run-time dash offset / array at their defaults
// (no dash on the BL2 side).
// ──────────────────────────────────────────────────────────────────────

#include <blend2d.h>
#include <chronon3d/scene/model/shape/path.hpp>

namespace chronon3d::blend2d_bridge::stroke_options {

/// Apply every stroke knob from `stroke` onto the BL2 context.
/// Call this immediately before `ctx.strokePath(...)`.  Idempotent
/// across multiple draw passes; safe to call as long as the BLContext
/// outlives the Apply call.
void apply_bl2_stroke_state(BLContext& ctx, const chronon3d::PathStroke& stroke);

} // namespace chronon3d::blend2d_bridge::stroke_options
