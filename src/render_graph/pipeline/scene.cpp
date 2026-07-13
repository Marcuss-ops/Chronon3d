// ---------------------------------------------------------------------------
// scene.cpp — Thin orchestrator for render_scene_via_graph() (Azione 19)
//
// Phase map (extracted to focused TUs):
//   0  scene_context_setup.cpp       — context + assets + camera + DebugConfig
//   1-3 scene_reuse_coordinator.cpp  — reuse fb + fingerprints + static fast-path
//   4-6  scene_dirty.cpp             — dirty rect + metrics
//   7   frame_reuse_policy.cpp       — empty dirty-rect reuse
//   8   graph_cache_coordinator.cpp  — build or reuse compiled graph
//   9   tile_execution_coordinator.cpp — tile decision + execute + fb
//   10  pool_preallocation.cpp       — preallocation
//   11  frame_timing_recorder.cpp    — phase timing telemetry
//   12  frame_state_commit.cpp       — commit state for next frame
// ===========================================================================

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/core/config.hpp>

#include "helpers.hpp"
#include "scene_internal.hpp"
#include "scene_context_setup.hpp"        // Azione 19 — Phase 0 helper
#include "scene_reuse_coordinator.hpp"    // Azione 19 — Phases 1+2+3 helper
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include "scene_fingerprint.hpp"
#include "frame_reuse_policy.hpp"
#include "graph_cache_coordinator.hpp"
#include "pool_preallocation.hpp"
#include "frame_state_commit.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include "dirty_telemetry_reporter.hpp"
#include "tile_execution_coordinator.hpp"
#include "frame_timing_recorder.hpp"
#include <chronon3d/core/scope/execution_scope.hpp>  // PR 6.2 — root scope

#include <spdlog/spdlog.h>
#include <cassert>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_scene_via_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    float fps,
    std::string_view diagnostic_label,
    chronon3d::SoftwareRenderer* sw_sidecar
) {
    ZoneScoped;
    const auto t0 = profiling::now();
    const auto hits_before = node_cache.stats().hits;

    // ── 0. Context setup ──
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps);
    profiling::ProfilingGuard profiling_guard(
        ctx.node_exec.counters, ctx.services.framebuffer_pool.get());
    SoftwareRenderer* sw_renderer =
        detail::setup_render_graph_context(ctx, scene, sw_sidecar);

    // ── 1-3. Resolved-scene reuse + Fingerprints + Static-scene fast-path ──
    auto reuse_eval = detail::evaluate_early_reuse_phases(
        ctx, scene, frame, static_cast<int>(width),
        static_cast<int>(height), sw_renderer);
    if (reuse_eval.fast_path_reuse_fb) {
        return reuse_eval.fast_path_reuse_fb;
    }

    // ── 4. Graph-structure hint + diagnostics plan ──
    // Inform graph_cache_coordinator + dirty-paths whether topology is
    // unchanged so full rebuild is skipped.  PR-A removed the
    // ExecutionPlan cache that used to gate on this inside GraphExecutor;
    // the flag survives for the downstream coordinator (audit §9.4).
    ctx.policy.graph_structure_unchanged =
        reuse_eval.scene_structure_unchanged &&
        !reuse_eval.static_cam_changed &&
        reuse_eval.frame_fp.active_at_fp != 0 &&
        reuse_eval.frame_fp.active_at_fp ==
            (sw_renderer ? sw_renderer->frame_history().prev_active_at_fingerprint : 0);

    if (settings.diagnostics.plan) {
        profiling::ProfilingGuard diag_guard(nullptr, profiling::g_current_framebuffer_pool);
        auto report = debug_preflight_render_graph(
            backend, node_cache, scene, camera, width, height,
            frame, frame_time, settings, registry, video_decoder,
            static_cast<float>(fps));
        spdlog::info("[graph-preflight] label='{}' frame={} size={}x{}\n{}",
                     diagnostic_label, static_cast<int>(frame), width, height, report.to_text());
        if (!settings.diagnostics.plan_output.empty()) {
            const auto report_path = format_plan_output_path(
                settings.diagnostics.plan_output, frame);
            if (write_plan_output_file(report_path, report.to_text())) {
                spdlog::info("[graph-preflight] report written to {}", report_path);
            }
        }
    }

    // ── 5. Resolve + dirty rect ──
    const auto t_resolve0 = profiling::now();
    const auto resolved = detail::resolve_layers(scene, ctx);
    const auto t_resolve1 = profiling::now();
    const auto t_dirty0 = profiling::now();
    auto dirty_out = detail::compute_dirty_rect(
        ctx, resolved, scene, settings, sw_renderer, frame, width, height);
    const auto t_dirty1 = profiling::now();

    // ── 6. Dirty metrics ──
    const double dirty_ratio = compute_and_apply_dirty_metrics(
        dirty_out, width, height, ctx.node_exec.counters, sw_renderer);
    log_dirty_debug(sw_renderer, ctx.policy.diagnostics_enabled, dirty_out, frame);
    ctx.node_exec.dirty_rect = dirty_out.dirty_rect;
    ctx.policy.reuse_prev_framebuffer = dirty_out.use_dirty_rects;

    // ── 7. Empty dirty-rect reuse ──
    {
        CHRONON_ZONE_C("dirty_fast_path_reuse", trace_category::kFrame);
        const Camera2_5D& cam = ctx.frame_input.camera_2_5d;
        auto reuse = evaluate_empty_dirty_reuse(
            sw_renderer, scene, frame, cam, reuse_eval.frame_fp,
            dirty_out, settings, width, height,
            ctx.policy.diagnostics_enabled);
        if (reuse.can_reuse) {
            sw_renderer->commit_prev_frame_state(
                frame, resolved.camera.camera,
                reuse.current_combined_fp, reuse.current_static_fp,
                reuse.current_structure_fp, reuse.current_active_at_fp,
                std::move(dirty_out.layer_bboxes));
            return reuse.framebuffer;
        }
    }

    // ── 8. Ping-pong framebuffers + scratch (must precede build for ALL paths) ──
    if (sw_renderer) {
        if (sw_renderer->config().scheduler().pingpong_framebuffer()) {
            setup_pingpong_buffers(sw_renderer, width, height);
            ctx.node_exec.ping_write = sw_renderer->buffer_ring().write_slot_view();
        }
        ctx.node_exec.transform_scratch = sw_renderer->scratch_buffer().slot_view(width, height);
    }

    // ── 9. Build or reuse compiled graph ──
    const auto t_graph0 = profiling::now();
    auto graph_result = build_or_reuse_graph(
        ctx, scene, resolved, width, height,
        ctx.policy.graph_structure_unchanged,
        ctx.policy.diagnostics_enabled);
    ctx.policy.skip_initial_clear = graph_result.skip_initial_clear;
    const auto t_graph1 = profiling::now();

    // ── 9a. Capture TextRunNode snapshots (chronon3d_cli inspect-text) ──
    // Snapshot while graph is fully built and frozen, before execution
    // mutates any state.  Real TextRunShape + world matrix + predicted bbox.
    if (sw_renderer) {
        sw_renderer->text_audit_snapshots().clear();
        const auto& graph = graph_result.compiled.graph;
        for (GraphNodeId i = 0; i < graph.size(); ++i) {
            if (!graph.has_node(i)) continue;
            const auto& node = graph.node(i);
            if (node.kind() != RenderGraphNodeKind::TextRun) continue;
            const auto* tr_node = static_cast<const TextRunNode*>(&node);
            if (!tr_node->shape()) continue;
            TextRunAuditSnapshot snap;
            snap.name = std::string(tr_node->name());
            snap.shape = tr_node->shape();
            snap.world_matrix = tr_node->placement().matrix;
            if (auto pred = tr_node->predicted_bbox(ctx, {})) {
                snap.predicted_bbox = Rect{
                    {static_cast<float>(pred->x0), static_cast<float>(pred->y0)},
                    {static_cast<float>(pred->x1 - pred->x0),
                     static_cast<float>(pred->y1 - pred->y0)}};
            } else {
                snap.predicted_bbox = Rect{
                    {0.0f, 0.0f},
                    {static_cast<float>(width), static_cast<float>(height)}};
            }
            snap.clip_rect = snap.predicted_bbox;
            sw_renderer->text_audit_snapshots().push_back(std::move(snap));
        }
    }

    // ── 10. Pre-frame pool preallocation ──
    const auto t_prealloc0 = profiling::now();
    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        const size_t prealloc_count = preallocate_for_frame(
            *sw_renderer->framebuffer_pool(), graph_result.compiled,
            width, height, ctx.node_exec.counters, ctx.policy.diagnostics_enabled);
        (void)prealloc_count;
    }
    const auto t_prealloc1 = profiling::now();

    // ── 11. Execute: tile-based (V1) or traditional single-pass ──
    // PR 6.2 — root ExecutionScope per render invocation.  Binds the
    // session + compiled-graph identity for the entire execute phase so
    // child scopes (tile / precomp) can walk the parent chain.  Arena
    // reset happens inside execute_tile_or_fallback at scope exit.
    RenderSession fallback_session;   // for !sw_renderer test paths
    auto& session_ref = sw_renderer
        ? static_cast<RenderSession&>(sw_renderer->session())
        : fallback_session;
    ExecutionScope root_scope = ExecutionScope::make_root(
        session_ref, session_ref.arena(),
        graph_result.compiled.graph_instance_id);
    const auto t_exec0 = profiling::now();
    auto exec_result = execute_tile_or_fallback(
        ctx, graph_result.compiled, resolved, settings, dirty_out,
        dirty_ratio, sw_renderer, frame, width, height, root_scope);
    const auto t_exec1 = profiling::now();

    // ── 12. Phase timing telemetry ──
    compute_and_record_timings(
        t_resolve0, t_resolve1, t_dirty0, t_dirty1,
        t_graph0, t_graph1, t_exec0, t_exec1,
        ctx.node_exec.counters, ctx.policy.diagnostics_enabled,
        frame, graph_result.graph_reused);
    record_dirty_telemetry(sw_renderer, dirty_out,
        exec_result.use_tile_execution, graph_result.graph_reused);

    // ── 13. Save state for next frame ──
    if (sw_renderer) {
        commit_frame_state(
            sw_renderer, frame, ctx.frame_input.camera_2_5d,
            std::move(graph_result.compiled), exec_result.fb,
            resolved, reuse_eval.frame_fp, dirty_out, width, height);
    }

    return exec_result.fb;
}

} // namespace chronon3d::graph
