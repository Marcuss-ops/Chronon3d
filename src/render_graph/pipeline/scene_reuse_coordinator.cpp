// ---------------------------------------------------------------------------
// scene_reuse_coordinator.cpp
//
// Implementation of detail::evaluate_early_reuse_phases() — Phases 1+2+3
// of render_scene_via_graph() extracted from scene.cpp (Azione 19).
//
// Coordinates:
//   * Phase 1: Resolved-scene reuse (consecutive frame with identical
//              fingerprint + active_at) — STRICT, BOTH static_fp AND
//              active_at_fp must match (consistent with original
//              comment in scene.cpp).
//   * Phase 2: Fingerprint computation (frame_fp + derived state:
//              scene_structure_unchanged / static_cam_changed / scene_is_static).
//   * Phase 3: Static-scene fast-path (unchanged scene + same camera +
//              same frame + matching fingerprints).
//
// The post-evaluate empty-dirty-rect path (Phase 7) depends on Phase 5's
// dirty_out and stays in scene.cpp.
// ---------------------------------------------------------------------------

#include "scene_reuse_coordinator.hpp"
#include "frame_reuse_policy.hpp"
#include "scene_fingerprint.hpp"
#include "camera_change_policy.hpp"  // chronon3d::graph::detail::camera_changed

#include <chronon3d/core/profiling/trace_categories.hpp>

namespace chronon3d::graph::detail {

ReuseEvaluation evaluate_early_reuse_phases(
    const RenderGraphContext& ctx,
    const Scene& scene,
    Frame frame,
    int width,
    int height,
    SoftwareRenderer* sw_renderer)
{
    ReuseEvaluation ev;

    if (!sw_renderer) {
        // Test paths / non-software backends: no fast-path, defaults.
        return ev;
    }

    const Camera2_5D& cam = ctx.frame_input.camera_2_5d;

    // ── Phase 1: Resolved-scene reuse ─────────────────────────────────
    // Must come BEFORE resolve_layers() and compute_dirty_rect() so that
    // identical consecutive frames avoid even entering the dirty system.
    //
    // CRITICAL: BOTH static_fp AND active_at_fp must match.  Using only
    // static_fp would incorrectly reuse frame 0's output for
    // DarkGridBackground frame 1 (active_at changes true→false but
    // static_fp matches).
    {
        CHRONON_ZONE_C("resolved_scene_reuse", trace_category::kFrame);

        FrameFingerprints reuse_fps = compute_frame_fingerprints(
            sw_renderer->scene_hasher(), scene, frame);

        auto reuse = evaluate_resolved_scene_reuse(
            sw_renderer, scene, frame, cam, reuse_fps,
            width, height, ctx.policy.diagnostics_enabled);

        if (reuse.can_reuse) {
            ev.fast_path_reuse_fb = std::move(reuse.framebuffer);
            // Fall through to populate derived state too, but caller
            // returns early when fast_path_reuse_fb set.
        }
    }

    // ── Phase 2: Fingerprints + derived state ──────────────────────────
    // Compute unconditionally so that frame_fp is populated even on the
    // first frame (previous chicken-and-egg gating broke the first-frame
    // history population).
    ev.frame_fp = compute_frame_fingerprints(
        sw_renderer->scene_hasher(), scene, frame);

    if (sw_renderer->frame_history().prev_static_scene_fingerprint != 0) {
        ev.scene_structure_unchanged =
            (ev.frame_fp.structure_fp == sw_renderer->frame_history().prev_graph_structure_fingerprint);

        ev.static_cam_changed = detail::camera_changed(
            cam, &sw_renderer->frame_history().prev_camera,
            sw_renderer->frame_history().prev_camera_valid);

        ev.scene_is_static = sw_renderer->scene_hasher().is_static_scene_at(scene, frame);

        // Save previous frame's fingerprints for comparison in static fast-path
        ev.prev_fp.static_fp = sw_renderer->frame_history().prev_static_scene_fingerprint;
        ev.prev_fp.active_at_fp = sw_renderer->frame_history().prev_active_at_fingerprint;
    }

    // ── Phase 3: Static-scene fast-path ───────────────────────────────
    if (!ev.fast_path_reuse_fb) {
        CHRONON_ZONE_C("static_scene_fast_check", trace_category::kFrame);

        auto reuse = evaluate_static_scene_fastpath(
            sw_renderer, scene, frame, cam, ev.frame_fp,
            ev.scene_structure_unchanged, ev.scene_is_static, ev.static_cam_changed,
            ev.prev_fp, width, height, ctx.policy.diagnostics_enabled);

        if (reuse.can_reuse) {
            ev.fast_path_reuse_fb = std::move(reuse.framebuffer);
        }
    }

    return ev;
}

} // namespace chronon3d::graph::detail
