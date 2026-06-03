#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>

namespace chronon3d {

struct CameraDebugOverlayOptions {
    bool show_target{true};
    bool show_safe_area{true};
    bool show_projected_bounds{true};
    bool show_layer_names{true};
    bool show_depth_order{true};
    bool show_camera_to_target_line{true};
};

void add_camera_debug_overlay(
    SceneBuilder& s,
    const CameraShotReport& report,
    const Camera2_5D& camera,
    const TransformResolverResult& resolved,
    Viewport viewport,
    CameraDebugOverlayOptions options = {}
);

} // namespace chronon3d

