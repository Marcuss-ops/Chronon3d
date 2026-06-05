#pragma once

// ---------------------------------------------------------------------------
// camera_debug_overlay_panels.hpp
//
// Panel drawing functions extracted from camera_debug_overlay.cpp.
// Each function draws one logical panel of the camera debug HUD.
// ---------------------------------------------------------------------------

#include <chronon3d/scene/camera/camera_debug_overlay.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d {

/// Shared context passed to all panel drawing functions.
struct OverlayContext {
    LayerBuilder& layer;
    const CameraShotReport& report;
    const Camera2_5D& camera;
    const TransformResolverResult& resolved;
    Viewport viewport;
    CameraDebugOverlayOptions options;
    const CameraPathVisualization* path;
};

// ── HUD panels (safe area, target, bounds, null/parent markers) ────

void draw_safe_area_and_target(const OverlayContext& ctx, bool& has_target, ScreenPoint& sp, Color& target_color);
void draw_camera_to_target_line(const OverlayContext& ctx, const ScreenPoint& sp, bool has_target, const Color& target_color);
void draw_null_parent_markers(const OverlayContext& ctx);
void draw_parent_child_links(const OverlayContext& ctx);
void draw_projected_bounds(const OverlayContext& ctx);

// ── Kinematic panels (jerk graph, path trace) ─────────────────────

void draw_jerk_graph(const OverlayContext& ctx);
void draw_path_trace(const OverlayContext& ctx);

// ── Spatial panels (top-down, side-view) ──────────────────────────

void draw_topdown_preview(const OverlayContext& ctx);
void draw_sideview_depth(const OverlayContext& ctx);

} // namespace chronon3d
