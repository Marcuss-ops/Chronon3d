#pragma once

// ──────────────────────────────────────────────────────────────────────
// blend2d_paint.hpp — PR3 single source-of-truth for color & gradient →
// Blend2D paint state.  Single canonical `inline` `to_bl_rgba(Color)` +
// single canonical `build_bl_gradient(Fill, ...)` helper, deduped out of
// 4 prior copies scattered across the legacy `text_processor_helpers.hpp`,
// `software_text_effects.cpp`, `text_run_processor.cpp`, and
// `text_rasterizer_render.cpp` (the last surviving reference to text
// rasterization will be removed wholesale in P1-7 Chore 2).
//
// Lives next to the existing `blend2d_bridge` family so future path
// stroking (PR4+ `BackendPolicy::kBlend2D`) can include this header
// transitively and get identical behavior.
//
// P1-7 Chore 1 (commit A): the two text sources (`text_processor_helpers.hpp`
// + `software_text_effects.cpp`) have been DELETED wholesale.  Remaining
// reference contracts:
//   - `text_run_processor.cpp`  (modern, `draw_text_run` path)
//   - `text_rasterizer_render.cpp`  (LEGACY; last surviving call site
//     of `BLGradient gradient(values, ...)`; Chore 2 deletes this TU)
// ──────────────────────────────────────────────────────────────────────

#include <blend2d.h>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <algorithm>
#include <optional>
#include <vector>

namespace chronon3d::blend2d_bridge::paint {

/// Discriminates `BLContext::setFillStyle` vs `BLContext::setStrokeStyle`
/// so one helper can cover both directions.  Lets callers fold the
/// legacy `apply_text_fill_style` / `apply_text_stroke_style` pair
/// (which were ~90-line textually-identical mirrors in
/// `text_rasterizer_render.cpp`) into a single `apply_text_style(...)`.
enum class StyleOp { Fill, Stroke };

// ── Color conversion ────────────────────────────────────────────────
//
// Canonical, ODR-safe `inline` definition (one instance across TUs).
// Callers in other namespaces re-export via:
//   `using chronon3d::blend2d_bridge::paint::to_bl_rgba;`

/// Convert a Chronon3d linear `Color` to Blend2D's 8-bit `BLRgba32`,
/// clamping each channel to [0, 255].
[[nodiscard]] inline BLRgba32 to_bl_rgba(const Color& c) noexcept {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

// ── Gradient construction ───────────────────────────────────────────

/// Build a vector of `BLGradientStop` from a `Fill::gradient.stops`.  The
/// returned vector can be passed straight into `BLGradient(..., n)` or
/// to `build_bl_gradient` which uses it internally.
[[nodiscard]] std::vector<BLGradientStop> build_gradient_stops(
    const chronon3d::Fill& fill);

/// Build a `BLGradient` from a `Fill`, in normalised rect coordinates:
///
/// |origin_x/origin_y| is the rectangle's top-left in the BL context's
/// current transform; |width| × |height| are the rectangle's dimensions.
/// The fill's gradient `from`/`to` are in [0..1] normalised space and
/// are scaled to those dimensions (SVG-style gradient geometry remap):
///
///   - LinearGradient:
///       x0 = origin_x + gradient.from.x * width
///       y0 = origin_y + gradient.from.y * height
///       x1 = origin_x + gradient.to.x   * width
///       y1 = origin_y + gradient.to.y   * height
///   - RadialGradient:
///       centre = (from * size)
///       radius = max(width, height) * max(0.001, to.x - from.x)
///   - ConicGradient:
///       centre = (from * size)
///       angle  = atan2(to - from)
///
/// Returns `std::nullopt` for degenerate input (Solid type, empty
/// stops, zero-area rect).  Caller should fall back to a solid colour
/// in that case.
///
/// SAFETY: `BLGradient(values, extend_mode, stops.data(), n)`
/// internally deep-copies the stops into a refcounted data block.
/// Returning the `BLGradient` by value from this function whose local
/// `stops` vector goes out of scope is therefore safe — the copies
/// are owned by Blend2D's runtime after construction.
[[nodiscard]] std::optional<BLGradient> build_bl_gradient(
    const chronon3d::Fill& fill,
    float origin_x,
    float origin_y,
    float width,
    float height);

} // namespace chronon3d::blend2d_bridge::paint
