// ---------------------------------------------------------------------------
// camera_debug_overlay.cpp — Orchestrator
//
// Panel drawing functions have been extracted to:
//   overlay_hud_panels.cpp      — safe area, target, bounds, null/parent markers
//   overlay_kinematic_panels.cpp — jerk graph, path trace
//   overlay_spatial_panels.cpp  — top-down XZ, side-view depth
// ---------------------------------------------------------------------------

#include <chronon3d/scene/camera/camera_debug_overlay.hpp>
#include "camera_debug_overlay_panels.hpp"

namespace chronon3d {

void add_camera_debug_overlay(
    SceneBuilder& s,
    const CameraShotReport& report,
    const Camera2_5D& camera,
    const ResolvedSceneTransforms& resolved,
    Viewport viewport,
    CameraDebugOverlayOptions options,
    const CameraPathVisualization* path
) {
    s.layer("camera_debug_hud", [&](LayerBuilder& l) {
        OverlayContext ctx{l, report, camera, resolved, viewport, options, path};

        bool has_target = false;
        ScreenPoint sp;
        Color target_color;

        draw_safe_area_and_target(ctx, has_target, sp, target_color);
        draw_jerk_graph(ctx);
        draw_path_trace(ctx);
        draw_camera_to_target_line(ctx, sp, has_target, target_color);
        draw_null_parent_markers(ctx);
        draw_parent_child_links(ctx);
        draw_topdown_preview(ctx);
        draw_sideview_depth(ctx);
        draw_projected_bounds(ctx);

        // ── Standardised diagnostic overlay ─────────────────────────
        // SAFETY: header default is `false` so production flows stay clean
        // of cyan crosshair/axes.  Camera-test orchestrator relies on seeing
        // EVERY HUD panel, so we EXPLICITLY enable all flags here.
        // Compositions that want a custom subset should pass their own
        // diag{} via a future add_camera_debug_overlay(... CameraDiagnosticOverlay) overload.
        CameraDiagnosticOverlay diag{
            .show_center_cross   = true,
            .show_target_marker  = true,
            .show_camera_axes    = true,
            .show_projected_bbox = true,
            .show_metrics_text   = true,
        };
        draw_diagnostic_overlay(ctx, diag);
    });
}

} // namespace chronon3d
