#pragma once

// ---------------------------------------------------------------------------
// diagnostics/bbox_overlay.hpp
//
// Single-purpose overlay: draws a thin 1px box around a raster::BBox.
// Used by the layout preview overlay and by tests that need a quick way
// to visualise predicted bounds without re-rendering the scene.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d::renderer::diagnostics {

/// Draw a 1px-wide axis-aligned box around `bbox` using `color`.
/// No-op for empty bboxes.
void draw_bbox_overlay(Framebuffer& fb, const raster::BBox& bbox, const Color& color);

} // namespace chronon3d::renderer::diagnostics
