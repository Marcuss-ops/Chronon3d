#pragma once

// ---------------------------------------------------------------------------
// diagnostics/nulls_overlay.hpp
//
// Visual aid for scenes that contain 2.5D null layers:
//   - parent -> child lines
//   - crosshair at each null layer position
//   - short x (red) / y (green) axis stubs for orientation
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

namespace chronon3d::renderer::diagnostics {

/// Render the null-layer diagnostic overlay onto `fb`. Uses `camera` to
/// project 2.5D world positions back into screen space.
void draw_null_overlay(Framebuffer& fb, const Scene& scene, const Camera2_5D& camera);

} // namespace chronon3d::renderer::diagnostics
