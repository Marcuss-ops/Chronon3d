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
    bool show_depth_side_view{true};
    OverlayAnchor anchor{OverlayAnchor::BottomRight};
    float panel_offset_x{0.0f};
    float panel_offset_y{0.0f};
};

/// Standardised diagnostic overlay for camera test compositions.
/// Draws: screen-center cross, target marker (coloured by error),
/// RGB camera axes, layer bounding boxes, and a metrics text panel.
/// Enabled by default via CameraDebugOverlayOptions.
struct CameraDiagnosticOverlay {
    bool show_center_cross{true};
    bool show_target_marker{true};
    bool show_camera_axes{true};
    bool show_projected_bbox{true};
    bool show_metrics_text{true};
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

/// Forward declaration — defined in camera_debug_overlay_panels.hpp.
struct OverlayContext;

/// Draw the standardised diagnostic overlay (center cross, target marker,
/// RGB axes, bboxes, metrics) inside the same LayerBuilder context as the
/// other HUD panels.  Called from add_camera_debug_overlay.
void draw_diagnostic_overlay(
    const OverlayContext& ctx,
    const CameraDiagnosticOverlay& diag
);

} // namespace chronon3d

