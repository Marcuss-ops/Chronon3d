// ---------------------------------------------------------------------------
// scene_commit_phase.cpp — Phase 5 of the render-scene-via-graph pipeline
//
// Save per-frame state to the SoftwareRenderer for the next frame:
// cached compiled graph, m_prev_framebuffer, fingerprints, camera, layer bboxes.
// ---------------------------------------------------------------------------

#include "scene_phases.hpp"

#include <memory>

namespace chronon3d::graph::detail {

void run_commit_phase(SceneRenderContext& sctx) {
    SoftwareRenderer* sw_renderer = sctx.sw_renderer;
    if (!sw_renderer) return;

    const Frame frame = sctx.frame;
    const Scene& scene = *sctx.scene;

    // Use combined_fp = static_fp ^ (active_at_fp * C) to match the fingerprint
    // stored in the resolved-reuse block above.  This ensures consistent
    // fingerprint comparison across frames (both fast-path and full-path save
    // the same format).

    // Cache render graph for incremental reuse next frame
    sw_renderer->m_cached_compiled_graph = std::make_unique<CompiledFrameGraph>(std::move(sctx.compiled));
    sw_renderer->m_cached_compiled_width  = sctx.width;
    sw_renderer->m_cached_compiled_height = sctx.height;
    sw_renderer->m_cached_compiled_structure_hash =
        sw_renderer->m_cached_compiled_graph->structure_hash;

    const uint64_t save_static_fp    = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
    const uint64_t save_structure_fp = sw_renderer->m_scene_hasher.compute_structure_fingerprint(scene);
    const uint64_t save_active_at_fp = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
    const uint64_t save_combined_fp  = save_static_fp ^ (save_active_at_fp * 0x9e3779b97f4a7c15ULL);

    // ── Update m_prev_framebuffer: swap ping-pong or assign directly ──
    // When the graph rendered into a ping buffer (ClearNode's ping-pong path),
    // swap indices so m_prev_framebuffer points to the completed frame.
    // Otherwise (first frame, tile execution, or COW fallback), fb_shared is
    // a pool FB and must be assigned directly.
    if (sctx.fb_shared.get() == sw_renderer->m_ping_fb[0] ||
        sctx.fb_shared.get() == sw_renderer->m_ping_fb[1]) {
        // Rendered into a ping — swap indices to advance the ping cycle.
        // swap_ping_indices() repoints m_prev_framebuffer to the new read ping
        // (the frame we just finished writing), making it the "previous" frame
        // for ClearNode's dirty-rect read in the next cycle.
        sw_renderer->swap_ping_indices();
    } else {
        // Rendered into a pool FB — assign directly.
        sw_renderer->m_prev_framebuffer = sctx.fb_shared;
    }

    sw_renderer->m_prev_layer_bboxes                = std::move(sctx.dirty_out.layer_bboxes);
    sw_renderer->m_prev_frame                       = frame;
    sw_renderer->m_prev_scene_fingerprint           = save_combined_fp;
    sw_renderer->m_prev_static_scene_fingerprint    = save_static_fp;
    sw_renderer->m_prev_graph_structure_fingerprint = save_structure_fp;
    sw_renderer->m_prev_active_at_fingerprint       = save_active_at_fp;
    sw_renderer->m_prev_camera                      = sctx.resolved.camera.camera;
    sw_renderer->m_prev_camera_valid                = sctx.resolved.camera.camera.enabled;
}

} // namespace chronon3d::graph::detail
