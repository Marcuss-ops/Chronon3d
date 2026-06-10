// ---------------------------------------------------------------------------
// scene_dirty_phase.cpp — Phase 2 of the render-scene-via-graph pipeline
//
// Full-path start: resolve layers, compute dirty rect, populate dirty ratio /
// counters / diagnostic info, and detect empty-dirty-rect fast-path reuse.
// ---------------------------------------------------------------------------

#include "scene_phases.hpp"
#include "scene_internal.hpp"
#include "../builder/graph_builder_internal.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <chrono>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

PhaseResult run_dirty_analysis_phase(SceneRenderContext& sctx) {
    SoftwareRenderer* sw_renderer = sctx.sw_renderer;
    const i32 width  = sctx.width;
    const i32 height = sctx.height;
    const Frame frame = sctx.frame;
    const Scene& scene = *sctx.scene;
    const RenderSettings& settings = *sctx.settings;

    // ── Resolve layers ──────────────────────────────────────────────────
    sctx.t_resolve0 = std::chrono::steady_clock::now();
    sctx.resolved = detail::resolve_layers(scene, sctx.ctx);
    sctx.t_resolve1 = std::chrono::steady_clock::now();

    // ── Compute dirty rect ──────────────────────────────────────────────
    sctx.t_dirty0 = std::chrono::steady_clock::now();
    sctx.dirty_out = detail::compute_dirty_rect(
        sctx.ctx, sctx.resolved, scene, settings, sw_renderer, frame, width, height);
    sctx.t_dirty1 = std::chrono::steady_clock::now();

    // ── Dirty ratio / counters / diagnostics ────────────────────────────
    sctx.dirty_ratio = 1.0;
    sctx.dirty_union_area_pixels = 0;
    if (sctx.dirty_out.dirty_rect) {
        const int dw = std::max(0, sctx.dirty_out.dirty_rect->x1 - sctx.dirty_out.dirty_rect->x0);
        const int dh = std::max(0, sctx.dirty_out.dirty_rect->y1 - sctx.dirty_out.dirty_rect->y0);
        const double area = static_cast<double>(dw) * static_cast<double>(dh);
        const double total = static_cast<double>(width) * height;
        if (total > 0) sctx.dirty_ratio = area / total;
        sctx.dirty_union_area_pixels = static_cast<u64>(area);
    }
    if (sctx.ctx.counters) {
        sctx.ctx.counters->dirty_union_area_pixels.store(
            sctx.dirty_union_area_pixels, std::memory_order_relaxed);
    }
    if (sw_renderer) {
        sw_renderer->m_last_dirty_area_ratio = sctx.dirty_ratio;
    }
    sctx.ctx.dirty_rect           = sctx.dirty_out.dirty_rect;
    sctx.ctx.reuse_prev_framebuffer = sctx.dirty_out.use_dirty_rects;

    if (sw_renderer && sctx.ctx.diagnostics_enabled) {
        if (sctx.dirty_out.dirty_rect) {
            spdlog::info(
                "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=[{},{} -> {},{}] prev_frame={}",
                static_cast<int>(frame),
                sctx.dirty_out.use_dirty_rects ? 1 : 0,
                sw_renderer->m_prev_framebuffer ? 1 : 0,
                sctx.dirty_out.dirty_rect->x0, sctx.dirty_out.dirty_rect->y0,
                sctx.dirty_out.dirty_rect->x1, sctx.dirty_out.dirty_rect->y1,
                static_cast<int>(sw_renderer->m_prev_frame));
        } else {
            spdlog::info(
                "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=null prev_frame={}",
                static_cast<int>(frame),
                sctx.dirty_out.use_dirty_rects ? 1 : 0,
                sw_renderer->m_prev_framebuffer ? 1 : 0,
                static_cast<int>(sw_renderer->m_prev_frame));
        }
    }

    // ── Fast-path reuse: empty dirty rect ───────────────────────────────
    sctx.fast_path_reuse = sw_renderer &&
                           settings.enable_dirty_rects &&
                           sctx.dirty_out.dirty_rect.has_value() &&
                           sctx.dirty_out.dirty_rect->is_empty() &&
                           sw_renderer->m_prev_framebuffer &&
                           sw_renderer->m_prev_framebuffer->width() == width &&
                           sw_renderer->m_prev_framebuffer->height() == height;

    if (sctx.fast_path_reuse) {
        if (sctx.ctx.diagnostics_enabled) {
            spdlog::info("[dirty-debug] frame={} fast_path_reuse=1", static_cast<int>(frame));
        }
        sw_renderer->m_last_dirty_area_ratio   = 0.0;
        sw_renderer->m_last_dirty_rect_enabled = true;
        sw_renderer->m_last_dirty_rect         = sctx.dirty_out.dirty_rect;
        sw_renderer->m_last_tile_execution_used = false;
        sw_renderer->m_last_fast_path_reused   = true;
        sw_renderer->m_last_graph_reused       = false;
        sw_renderer->m_prev_layer_bboxes       = std::move(sctx.dirty_out.layer_bboxes);
        sw_renderer->m_prev_frame              = frame;
        // Use combined_fp for consistency with resolved-reuse and full-path save
        const uint64_t fp_static    = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        const uint64_t fp_active_at = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
        const uint64_t fp_combined  = fp_static ^ (fp_active_at * 0x9e3779b97f4a7c15ULL);
        sw_renderer->m_prev_scene_fingerprint          = fp_combined;
        sw_renderer->m_prev_static_scene_fingerprint   = fp_static;
        sw_renderer->m_prev_active_at_fingerprint      = fp_active_at;
        sw_renderer->m_prev_camera                     = sctx.resolved.camera.camera;
        sw_renderer->m_prev_camera_valid               = sctx.resolved.camera.camera.enabled;
        return PhaseResult::Done;
    }

    return PhaseResult::Continue;
}

} // namespace chronon3d::graph::detail
