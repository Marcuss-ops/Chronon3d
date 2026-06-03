#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <vector>

namespace chronon3d {

struct CameraPathJerkSample {
    Vec3 position{0.0f, 0.0f, 0.0f};
    float jerk{0.0f};
};

struct CameraPathVisualization {
    std::vector<CameraPathJerkSample> samples;
    int current_frame{0};
    int total_frames{90};
};

enum class OverlayAnchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

struct CameraDebugOverlayOptions {
    bool show_target{true};
    bool show_safe_area{true};
    bool show_projected_bounds{true};
    bool show_layer_names{true};
    bool show_depth_order{true};
    bool show_camera_to_target_line{true};
    bool show_camera_path{true};
    bool show_projected_path{true};
    bool show_topdown_preview{true};
    OverlayAnchor anchor{OverlayAnchor::BottomRight};
    float panel_offset_x{0.0f};
    float panel_offset_y{0.0f};
};

void add_camera_debug_overlay(
    SceneBuilder& s,
    const CameraShotReport& report,
    const Camera2_5D& camera,
    const TransformResolverResult& resolved,
    Viewport viewport,
    CameraDebugOverlayOptions options = {},
    const CameraPathVisualization* path = nullptr
);

} // namespace chronon3d

