// ---------------------------------------------------------------------------
// scene_frame_reuse.cpp — Phase 1 of the render-scene-via-graph pipeline
//
// Tries to short-circuit the entire render by returning the previous frame's
// framebuffer unchanged.  Covers:
//   1a. Resolved scene reuse (consecutive / same frame with matching
//       combined fingerprint).
//   1b. Fingerprint pre-computation (used by Phase 2 as well).
//   1c. Static scene fast-path (structure + camera + active_at unchanged,
//       consecutive frame).
// ---------------------------------------------------------------------------

#include "scene_phases.hpp"
#include "helpers.hpp"
#include "scene_internal.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

PhaseResult run_frame_reuse_phase(SceneRenderContext& sctx) {
    SoftwareRenderer* sw_renderer = sctx.sw_renderer;
    if (!sw_renderer) return PhaseResult::Continue;

    const i32 width  = sctx.width;
    const i32 height = sctx.height;
    const Frame frame = sctx.frame;
    const Scene& scene = *sctx.scene;

    // ── 1a. Resolved scene reuse: consecutive/same frame ────────────────
    // If the already-evaluated scene produces identical visual output as the
    // previous frame, skip resolve_layers(), dirty computation, graph build,
    // graph execution and compositing entirely.
    //
    // CRITICAL: We require BOTH static_fp AND active_at_fp to match.
    // - static_fp confirms layer structure is unchanged
    // - active_at_fp confirms which layers are active at each frame is unchanged
    //
    // Using only static_fp would incorrectly reuse frame 0's output for
    // DarkGridBackground frame 1 (active_at changes from true→false, but
    // static_fp matches since structure is the same — yet frame 1's evaluated
    // scene is empty, visually different).
    //
    // This block must come BEFORE resolve_layers() and compute_dirty_rect() so
    // that identical consecutive frames avoid even entering the dirty system.
    if (sw_renderer->m_prev_framebuffer &&
        sw_renderer->m_prev_framebuffer->width()  == width &&
        sw_renderer->m_prev_framebuffer->height() == height &&
        (sw_renderer->m_prev_frame == frame ||
         (sw_renderer->m_prev_frame == frame - 1 &&
          sw_renderer->m_scene_hasher.is_static_scene_at(scene, frame))))
    {
        CHRONON_ZONE_C("resolved_scene_reuse", trace_category::kFrame);
        const Camera2_5D& cam = sctx.ctx.camera_2_5d;
        const bool cam_changed = detail::camera_changed(
            cam, &sw_renderer->m_prev_camera, sw_renderer->m_prev_camera_valid);
        const uint64_t static_fp   = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        const uint64_t active_at_fp = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
        const uint64_t combined_fp  = static_fp ^ (active_at_fp * 0x9e3779b97f4a7c15ULL);

        if (!cam_changed && sw_renderer->m_prev_scene_fingerprint == combined_fp) {
            sw_renderer->m_last_dirty_area_ratio     = 0.0;
            sw_renderer->m_last_dirty_rect_enabled    = false;
            sw_renderer->m_last_dirty_rect            = std::nullopt;
            sw_renderer->m_last_tile_execution_used   = false;
            sw_renderer->m_last_fast_path_reused      = true;
            sw_renderer->m_last_graph_reused          = false;
            sw_renderer->m_prev_frame                 = frame;
            sw_renderer->m_prev_scene_fingerprint     = combined_fp;
            sw_renderer->m_prev_camera                = cam;
            sw_renderer->m_prev_camera_valid          = cam.enabled;
            if (sctx.ctx.counters) {
                sctx.ctx.counters->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
                sctx.ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                sctx.ctx.counters->clear_skipped_pixels.fetch_add(
                    static_cast<uint64_t>(width) * height,
                    std::memory_order_relaxed);
            }
            if (sctx.ctx.diagnostics_enabled) {
                spdlog::info("[resolved-reuse] frame={} combined_fingerprint_match=1",
                    static_cast<int>(frame));
            }
            return PhaseResult::Done;
        }
    }

    // ── 1b. Fingerprints (pre-computed for fast-paths + executor hint) ──
    // Compute once and reuse: the content-sensitive static fast-path, the
    // structure-only graph reuse path, the dirty-rect fast-path, and the
    // execution plan cache hint all need to know whether the scene structure
    // and camera are unchanged since the previous frame.
    if (sw_renderer->m_prev_static_scene_fingerprint != 0) {
        sctx.current_static_fp      = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        sctx.current_structure_fp   = sw_renderer->m_scene_hasher.compute_structure_fingerprint(scene);
        sctx.scene_structure_unchanged =
            (sctx.current_structure_fp == sw_renderer->m_prev_graph_structure_fingerprint);
        const Camera2_5D& cam = sctx.ctx.camera_2_5d;
        sctx.static_cam_changed = detail::camera_changed(
            cam, &sw_renderer->m_prev_camera, sw_renderer->m_prev_camera_valid);
        // Use frame-aware static check so that animations which have reached
        // their terminal state (e.g. tracking_breathing at frame 120+) are
        // treated as static, enabling consecutive-frame fast-path reuse.
        sctx.scene_is_static = sw_renderer->m_scene_hasher.is_static_scene_at(scene, frame);
        sctx.current_active_at_fp =
            sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
        sctx.active_at_unchanged =
            (sctx.current_active_at_fp != 0) &&
            (sctx.current_active_at_fp == sw_renderer->m_prev_active_at_fingerprint);
    }

    // ── 1c. Static scene fast-path (no dirty rects required) ────────────
    // When the scene is unchanged and the camera is the same, skip graph
    // building + execution entirely — return the previous framebuffer.
    //
    // For truly static scenes (no AnimatedTransform, no Video layers, no
    // transitions, no time-dependent expressions), we also allow consecutive
    // frame reuse (m_prev_frame == frame - 1).  This enables the fast-path
    // to work in real video exports (frames 0,1,2,3...) not just benchmarks
    // where frame is always 0.  For frame-dependent scenes (transitions,
    // animations), we require exact frame match for safety.
    //
    // CRITICAL: The active_at_fingerprint check is required because
    // compute_static_fingerprint() hashes ALL layers regardless of
    // active_at(frame). Scenes where layers activate/deactivate across frames
    // (e.g. DarkGridBackground with duration=1) would otherwise incorrectly
    // match — returning stale output.
    const bool frame_reuse =
        (sw_renderer->m_prev_frame == frame) ||
        (sctx.scene_is_static &&
         sw_renderer->m_prev_frame == frame - 1);

    if (sw_renderer->m_prev_framebuffer &&
        sw_renderer->m_prev_framebuffer->width()  == width &&
        sw_renderer->m_prev_framebuffer->height() == height &&
        frame_reuse &&
        sctx.scene_structure_unchanged &&
        !sctx.static_cam_changed &&
        sctx.active_at_unchanged &&
        sw_renderer->m_prev_static_scene_fingerprint != 0 &&
        sctx.current_static_fp == sw_renderer->m_prev_static_scene_fingerprint)
    {
        CHRONON_ZONE_C("static_scene_fast_check", trace_category::kFrame);
        sw_renderer->m_last_dirty_area_ratio     = 0.0;
        sw_renderer->m_last_dirty_rect_enabled   = false;
        sw_renderer->m_last_dirty_rect           = std::nullopt;
        sw_renderer->m_last_tile_execution_used  = false;
        sw_renderer->m_last_fast_path_reused     = true;
        sw_renderer->m_last_graph_reused         = false;
        sw_renderer->m_prev_frame                = frame;
        sw_renderer->m_prev_camera               = sctx.ctx.camera_2_5d;
        sw_renderer->m_prev_camera_valid         = sctx.ctx.camera_2_5d.enabled;
        if (sctx.ctx.counters) {
            sctx.ctx.counters->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
            sctx.ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
            sctx.ctx.counters->clear_skipped_pixels.fetch_add(
                static_cast<uint64_t>(width) * height,
                std::memory_order_relaxed);
        }
        if (sctx.ctx.diagnostics_enabled) {
            spdlog::info("[static-fastpath] frame={} static_fingerprint_match=1",
                static_cast<int>(frame));
        }
        return PhaseResult::Done;
    }

    return PhaseResult::Continue;
}

} // namespace chronon3d::graph::detail
