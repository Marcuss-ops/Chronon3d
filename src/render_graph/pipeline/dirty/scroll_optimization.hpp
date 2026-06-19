#pragma once

// ── Scrolling shortcut for dirty-rect tracking ─────────────────────────────
//
/// @file    scroll_optimization.hpp
/// @brief   When only the camera position has changed by an integer amount
///          we can shift the previous framebuffer and only re-render the
///          newly exposed strip.
///
/// Extracted from scene_dirty_helpers.hpp.

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/types.hpp>
#include <optional>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph::detail {

/// Try to optimize the dirty rect by shifting the previous framebuffer
/// by the integer camera delta and re-rendering only the exposed strip.
/// Returns the strip bbox on success, or std::nullopt if scroll is not
/// applicable (camera params changed, non-integer delta, etc.).
[[nodiscard]] std::optional<raster::BBox> try_scroll_optimization(
    SoftwareRenderer* sw_renderer,
    const Camera2_5D& cam25d,
    i32 width,
    i32 height);

} // namespace chronon3d::graph::detail
