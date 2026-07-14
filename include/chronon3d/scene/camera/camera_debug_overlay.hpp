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
    bool enable_overlay{false};
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
///
/// SAFETY: All flags default to FALSE so production renders stay clean.
/// Camera-test compositions opt in by passing an explicit diag{}
/// with the desired boolean set.  Final renders MUST NOT show any
/// of these HUD elements.
struct CameraDiagnosticOverlay {
    bool show_center_cross{false};
    bool show_target_marker{false};
    bool show_camera_axes{false};
    bool show_projected_bbox{false};
    bool show_metrics_text{false};
};

void add_camera_debug_overlay(
    SceneBuilder& s,
    const CameraShotReport& report,
    const Camera2_5D& camera,
    const ResolvedSceneTransforms& resolved,
    Viewport viewport,
    CameraDebugOverlayOptions options = {},
    const CameraPathVisualization* path = nullptr
);

/// Shared context passed to all panel drawing functions.
struct OverlayContext {
    LayerBuilder& layer;
    const CameraShotReport& report;
    const Camera2_5D& camera;
    const ResolvedSceneTransforms& resolved;
    Viewport viewport;
    CameraDebugOverlayOptions options;
    const CameraPathVisualization* path;
};

/// Draw the standardised diagnostic overlay (center cross, target marker,
/// RGB axes, bboxes, metrics) inside the same LayerBuilder context as the
/// other HUD panels.  Called from add_camera_debug_overlay.
void draw_diagnostic_overlay(
    const OverlayContext& ctx,
    const CameraDiagnosticOverlay& diag
);

/// Canvas-relative bounds for the diagnostic overlay panel.  Pure data
/// struct returned by `compute_overlay_panel_constraints()`; useful for
/// downstream LAYOUT_2D code to know where the panel sits on the canvas
/// without invoking the side-effecting `add_camera_debug_overlay()`
/// (which calls into SceneBuilder layers).  Defaults match the canonical
/// panel size used by the diagnostic overlay (320x240 px).
struct OverlayCanvasBounds {
    float x{0.0f};
    float y{0.0f};
    float width{320.0f};
    float height{240.0f};
};

/// Compute the canvas-relative bounds for the diagnostic overlay panel.
/// Pure function (no side effects, no internal state) over
/// `CameraDebugOverlayOptions` + `Viewport`.  Always-defined regardless
/// of `CHRONON3D_ENABLE_DIAGNOSTICS`: production layouts benefit equally
/// from querying the panel bounds.  Honours `options.anchor`:
///   - TopLeft     -> ( options.panel_offset_x,                     options.panel_offset_y)
///   - TopRight    -> (vp_w - panel_w - options.panel_offset_x,     options.panel_offset_y)
///   - BottomLeft  -> ( options.panel_offset_x,                     vp_h - panel_h - options.panel_offset_y)
///   - BottomRight -> (vp_w - panel_w - options.panel_offset_x,     vp_h - panel_h - options.panel_offset_y)
/// `panel_width` / `panel_height` default to 320x240 (the canonical
/// overlay panel size); custom sizes are accepted (e.g. for compact
/// overlays on portrait viewports).
inline OverlayCanvasBounds compute_overlay_panel_constraints(
    const CameraDebugOverlayOptions& options,
    Viewport viewport,
    float panel_width = 320.0f,
    float panel_height = 240.0f
) {
    OverlayCanvasBounds b{};
    b.width  = panel_width;
    b.height = panel_height;
    const float vp_w = static_cast<float>(viewport.width);
    const float vp_h = static_cast<float>(viewport.height);
    switch (options.anchor) {
        case OverlayAnchor::TopLeft:
            b.x = options.panel_offset_x;
            b.y = options.panel_offset_y;
            break;
        case OverlayAnchor::TopRight:
            b.x = vp_w - panel_width - options.panel_offset_x;
            b.y = options.panel_offset_y;
            break;
        case OverlayAnchor::BottomLeft:
            b.x = options.panel_offset_x;
            b.y = vp_h - panel_height - options.panel_offset_y;
            break;
        case OverlayAnchor::BottomRight:
            b.x = vp_w - panel_width - options.panel_offset_x;
            b.y = vp_h - panel_height - options.panel_offset_y;
            break;
    }
    return b;
}

#ifndef CHRONON3D_ENABLE_DIAGNOSTICS

// ── No-op stubs when engine-level diagnostics are disabled at compile time ──

inline void add_camera_debug_overlay(
    SceneBuilder& /*s*/,
    const CameraShotReport& /*report*/,
    const Camera2_5D& /*camera*/,
    const ResolvedSceneTransforms& /*resolved*/,
    Viewport /*viewport*/,
    CameraDebugOverlayOptions /*options*/,
    const CameraPathVisualization* /*path*/
) {}

inline void draw_diagnostic_overlay(
    const OverlayContext& /*ctx*/,
    const CameraDiagnosticOverlay& /*diag*/
) {}

#endif // CHRONON3D_ENABLE_DIAGNOSTICS

} // namespace chronon3d

