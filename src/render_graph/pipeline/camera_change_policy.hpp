#pragma once

// ── Camera change detection policy ─────────────────────────────────────────
//
/// @file    camera_change_policy.hpp
/// @brief   Compares the current camera with the previously stored state to
///          decide whether the scene graph needs to be rebuilt.
///
/// Extracted from scene_internal.hpp so this policy can be unit-tested and
/// evolved independently of the dirty-rect pipeline.

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

namespace chronon3d::graph::detail {

/// Returns true when the camera has changed compared to the previous frame's
/// stored state.  A disabled camera that was also disabled before does NOT
/// count as a change.  Enabled → disabled, disabled → enabled, or any
/// parameter change on an enabled camera all count as changes.
[[nodiscard]] bool camera_changed(
    const Camera2_5D& current,
    const Camera2_5D* prev,
    bool prev_valid
);

} // namespace chronon3d::graph::detail
