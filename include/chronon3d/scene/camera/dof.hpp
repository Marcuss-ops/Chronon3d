#pragma once

#include <chronon3d/scene/camera/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

[[nodiscard]] inline f32 compute_dof_blur_radius(const DepthOfFieldSettings& dof, f32 layer_world_z) {
    if (!dof.enabled) {
        return 0.0f;
    }

    const f32 dist = std::abs(layer_world_z - dof.focus_z);
    const f32 blur = dist * dof.aperture;
    return std::clamp(blur, 0.0f, dof.max_blur);
}

} // namespace chronon3d
