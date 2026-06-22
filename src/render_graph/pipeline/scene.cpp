// ---------------------------------------------------------------------------
// scene.cpp — Thin orchestrator for render_scene_via_graph()
//
// Delegates to focused modules (each handles one phase):
//   frame_reuse_policy.cpp           — Phase 1-3: fast-path frame reuse checks
//   scene_fingerprint.cpp            — Fingerprint computation
//   scene_dirty.cpp                  — Phase 4: dirty rect computation
//   dirty_telemetry_reporter.cpp     — Dirty ratio/counters/debug logging
//   graph_cache_coordinator.cpp      — Phase 5: build or reuse compiled graph
//   pool_preallocation.cpp           — Phase 6: preallocate framebuffers
//   tile_execution_coordinator.cpp   — Tile decision + execution + counters
//   frame_timing_recorder.cpp        — Phase timing telemetry + diagnostics
//   frame_state_commit.cpp           — Commit state + telemetry
//   scene_refresh.cpp/hpp            — Refresh compiled graph payloads
//   debug.cpp                        — Diagnostic graph analysis
//   composition.cpp                  — render_composition_frame (handles SSAA, MB)
// ===========================================================================

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/core/config.hpp>

#include "helpers.hpp"
#include "scene_internal.hpp"
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
    std::string_view diagnostic_label
) {
    ZoneScoped;
    const auto t0 = profiling::now();
    const auto hits_before = node_cache.stats().hits;

    // ── 0. Graph context + camera ────────────────────────────────────────
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );

    // TICKET-007 — single per-instance seeding point for the DebugConfig
    // pointer.  Every code path that reads `ctx.policy.debug_config`
    // (GlowPipeline::render, future text-bbox overlay relay, etc.)
    // now reflects the OWNING engine's debug flags instead of the
    // previously-removed process-wide `detail::g_debug_config`.
    // When the backend is a non-SoftwareRenderer render backend,
    // debug_cfg remains nullptr and overlays are skipped — the
    // safe default for non-software backends and matches the
    // pre-existing test contract (e.g. tests/text/test_text_material.cpp).


    // Thread the per-composition assets root through the render context.
    // Deep code should use ctx.resolve_asset() or ctx.frame_input.assets_root
    // instead of the deprecated AssetRegistry::resolve() TLS API.
    if (const auto& root = scene.assets_root(); !root.empty()) {
        ctx.frame_input.assets_root = root.string();
    }

    ctx.frame_input.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.frame_input.camera_2_5d = resolved_camera.camera;
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.projection_ctx = renderer::make_projection_context(
            ctx.frame_input.camera_2_5d, ctx.frame_input.width, ctx.frame_input.height);
        ctx.frame_input.projection_ctx.ready = true;
    }

    // RAII guard: sets profiling thread-locals and restores on any exit path
    profiling::ProfilingGuard profiling_guard(
        ctx.node_exec.counters, ctx.services.framebuffer_pool.get());

    // WP-6 / TICKET-018 closeout — audit-prescribed migration
    //         (`docs/audits/2026-06-software-renderer-inventory.md` §"dynamic_cast /
    //         static_cast verso SoftwareRenderer" — scene.cpp:106).  Inherits
    //         gracefully when the runtime's RenderBackend is a SoftwareRenderer
    //         instance; null otherwise (legacy SoftwareBackend caller ⇒
    //         renders as if no renderer hint were provided).  The previous
    //         `= sw_sidecar` referred to a parameter that does not exist on
    //         `render_scene_via_graph`; this compilation error was one of the
    //         the pre-existing cascade regressions addressed by TICKET-018.
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);

    // TICKET-007 - single per-instance seeding point for the DebugConfig
    // pointer.  Every code path that reads `ctx.policy.debug_config`
    // (GlowPipeline::render, future text-bbox overlay relay, etc.)
    // now reflects the OWNING engine's debug flags instead of the
    // previously-removed process-wide `detail::g_debug_config`.
    // When the backend is a non-SoftwareRenderer render backend,
    // debug_cfg remains nullptr and overlays are skipped -
    // the safe default for non-software backends and matches the
    // pre-existing test contract (e.g. tests/text/test_text_material.cpp).
    if (sw_renderer) {
        ctx.policy.debug_config = &sw_renderer->config().debug();
    }

    // Wire compiled_graph_cache + node_catalog + effect_catalog + scheduler
    // into the render graph context so that graph_cache_coordinator,
    // PrecompNode creation, and dirty/tile policies can access them
    // without a SoftwareRenderer dependency.
    if (sw_renderer) {
        ctx.services.compiled_graph_cache = &sw_renderer->graph_cache();
        ctx.services.node_catalog = &sw_renderer->graph_node_registry();
        ctx.services.effect_catalog = &sw_renderer->effect_catalog();
        // ── PR-B: propagate scheduler to nested graph call sites ──────
        // PrecompNode dereferences `*ctx.services.scheduler` to route its
        // inner execute() through the same arena as the parent graph.
        ctx.services.scheduler = &sw_renderer->scheduler();
        // PR-5 — propagate session pointer so PrecompNode can borrow the
        // program_store for cache lookups.
        ctx.services.session = &sw_renderer->session();
        // PR-1 — production paths must have a wired scheduler after
        // pipeline wiring.  Test paths (no SoftwareRenderer) create
        // a local Sequential(1) scheduler.
        assert(ctx.services.scheduler != nullptr);
    }

    // ── 1. Fast-path: Resolved-scene reuse (consecutive / same frame) ───
    // Must come BEFORE resolve_layers() and compute_dirty_rect() so that
    // identical consecutive frames avoid even entering the dirty system.
    //
    // CRITICAL: We require BOTH static_fp AND active_at_fp to match.
    // Using only static_fp would incorrectly reuse frame 0's output for
    // DarkGridBackground frame 1 (active_at changes from true→false, but
    // static_fp matches since structure is the same).
    {
        CHRONON_ZONE_C("resolved_scene_reuse", trace_category::kFrame);
        const Camera2_5D& cam = ctx.frame_input.camera_2_5d;
        FrameFingerprints reuse_fps;
        if (sw_renderer) {
            reuse_fps = compute_frame_fingerprints(
                sw_renderer->scene_hasher(), scene, frame);
        }

        auto reuse = evaluate_resolved_scene_reuse(
            sw_renderer, scene, frame, cam, reuse_fps,
            width, height, ctx.policy.diagnostics_enabled);

        if (reuse.can_reuse) {
            return reuse.framebuffer;
        }
    }

    // ── 2. Fingerprints (pre-computed for fast-paths + executor hint) ────
    // Compute once and reuse: the content-sensitive static fast-path, the
    // structure-only graph reuse path, the dirty-rect fast-path, and the
    // execution plan cache hint all need to know whether the scene structure
    // and camera are unchanged since the previous frame.
    //
    // State carried forward to downstream phases:
    //   scene_structure_unchanged — graph topology unchanged
    //   scene_is_static           — no animated content
    //   static_cam_changed        — camera moved since last frame
    bool scene_structure_unchanged = false;
    bool static_cam_changed = true;
    bool scene_is_static = false;
    FrameFingerprints frame_fp;
    FrameFingerprints prev_fp; // populated with zero if no history

    // Compute fingerprints unconditionally so that frame_fp is populated even
    // on the first frame.  Previously the computation was gated by
    // prev_static_scene_fingerprint != 0, which created a chicken-and-egg
    // problem: fingerprints were never computed for the first frame, so the
    // history was never populated, preventing all subsequent frames from
    // benefiting from the graph cache.
    if (sw_renderer) {
        frame_fp = compute_frame_fingerprints(
            sw_renderer->scene_hasher(), scene, frame);

        if (sw_renderer->frame_history().prev_static_scene_fingerprint != 0) {
            scene_structure_unchanged =
                (frame_fp.structure_fp == sw_renderer->frame_history().prev_graph_structure_fingerprint);

            const Camera2_5D& cam = ctx.frame_input.camera_2_5d;
            static_cam_changed = detail::camera_changed(
                cam, &sw_renderer->frame_history().prev_camera,
                sw_renderer->frame_history().prev_camera_valid);

            scene_is_static = sw_renderer->scene_hasher().is_static_scene_at(scene, frame);

            // Save previous frame's fingerprints for comparison in static fast-path
            prev_fp.static_fp = sw_renderer->frame_history().prev_static_scene_fingerprint;
            prev_fp.active_at_fp = sw_renderer->frame_history().prev_active_at_fingerprint;
        }
    }

    // ── 3. Fast-path: Static scene (no dirty rects required) ─────────────
    {
        CHRONON_ZONE_C("static_scene_fast_check", trace_category::kFrame);
        const Camera2_5D& cam = ctx.frame_input.camera_2_5d;

        auto reuse = evaluate_static_scene_fastpath(
            sw_renderer, scene, frame, cam, frame_fp,
            scene_structure_unchanged, scene_is_static, static_cam_changed,
            prev_fp, width, height, ctx.policy.diagnostics_enabled);

        if (reuse.can_reuse) {
            return reuse.framebuffer;
        }
    }

    // ── 4. Full path: graph structure hint + diagnostics plan ────────────
    // Inform downstream phases (graph_cache_coordinator, dirty-paths)
    // whether the graph topology is unchanged so that full graph rebuild
    // can be skipped.  PR-A removed the ExecutionPlan cache that used to
    // gate on this flag inside GraphExecutor; the flag survives for the
    // downstream coordinator and will be re-paired with a stable fast-path
    // in a future PR (audit §9.4).
    ctx.policy.graph_structure_unchanged = scene_structure_unchanged &&
        !static_cam_changed &&
        frame_fp.active_at_fp != 0 &&
        frame_fp.active_at_fp == (sw_renderer ? sw_renderer->frame_history().prev_active_at_fingerprint : 0);

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

    // ── 5. Resolve + dirty rect ─────────────────────────────────────────
    const auto t_resolve0 = profiling::now();
    const auto resolved = detail::resolve_layers(scene, ctx);
    const auto t_resolve1 = profiling::now();

    const auto t_dirty0 = profiling::now();
    auto dirty_out = detail::compute_dirty_rect(
        ctx, resolved, scene, settings, sw_renderer, frame, width, height);
    const auto t_dirty1 = profiling::now();

    // ── Dirty metrics ───────────────────────────────────────────────────
    const double dirty_ratio = compute_and_apply_dirty_metrics(
        dirty_out, width, height, ctx.node_exec.counters, sw_renderer);
    log_dirty_debug(sw_renderer, ctx.policy.diagnostics_enabled, dirty_out, frame);
    ctx.node_exec.dirty_rect = dirty_out.dirty_rect;
    ctx.policy.reuse_prev_framebuffer = dirty_out.use_dirty_rects;

    // ── 6. Fast-path: Empty dirty-rect reuse ─────────────────────────────
    {
        CHRONON_ZONE_C("dirty_fast_path_reuse", trace_category::kFrame);
        const Camera2_5D& cam = ctx.frame_input.camera_2_5d;
        auto reuse = evaluate_empty_dirty_reuse(
            sw_renderer, scene, frame, cam, frame_fp,
            dirty_out, settings, width, height,
            ctx.policy.diagnostics_enabled);

        if (reuse.can_reuse) {
            sw_renderer->commit_prev_frame_state(
                frame, resolved.camera.camera,
                reuse.current_combined_fp,
                reuse.current_static_fp,
                reuse.current_structure_fp,
                reuse.current_active_at_fp,
                std::move(dirty_out.layer_bboxes));
            return reuse.framebuffer;
        }
    }

    // ── 7. Wire up ping-pong framebuffers + scratch ─────────────────────
    // Both must be set before graph build/reuse for ALL code paths.
    if (sw_renderer) {
        if (sw_renderer->config().scheduler().pingpong_framebuffer()) {
            setup_pingpong_buffers(sw_renderer, width, height);
            ctx.node_exec.ping_write = sw_renderer->buffer_ring().write_slot_view();
        }
        ctx.node_exec.transform_scratch = sw_renderer->scratch_buffer().slot_view(width, height);
    }

    // ── 8. Build or reuse compiled graph ─────────────────────────────────
    const auto t_graph0 = profiling::now();
    auto graph_result = build_or_reuse_graph(
        ctx, scene, resolved, width, height,
        ctx.policy.graph_structure_unchanged,
        ctx.policy.diagnostics_enabled);
    ctx.policy.skip_initial_clear = graph_result.skip_initial_clear;
    const auto t_graph1 = profiling::now();

    // ── 9. Pre-frame pool preallocation ──────────────────────────────────
    const auto t_prealloc0 = profiling::now();
    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        const size_t prealloc_count = preallocate_for_frame(
            *sw_renderer->framebuffer_pool(),
            graph_result.compiled,
            width, height,
            ctx.node_exec.counters,
            ctx.policy.diagnostics_enabled);
        (void)prealloc_count;
    }
    const auto t_prealloc1 = profiling::now();

    // ── 10. Execute: tile-based (V1) or traditional single-pass ──────────
    const auto t_exec0 = profiling::now();
    auto exec_result = execute_tile_or_fallback(
        ctx, graph_result.compiled, resolved, settings, dirty_out,
        dirty_ratio, sw_renderer, frame, width, height);
    const auto t_exec1 = profiling::now();

    // ── 11. Phase timing telemetry ───────────────────────────────────────
    compute_and_record_timings(
        t_resolve0, t_resolve1, t_dirty0, t_dirty1,
        t_graph0, t_graph1, t_exec0, t_exec1,
        ctx.node_exec.counters, ctx.policy.diagnostics_enabled,
        frame, graph_result.graph_reused);

    // ── Record per-frame dirty-rect telemetry ────────────────────────────
    record_dirty_telemetry(sw_renderer, dirty_out,
        exec_result.use_tile_execution, graph_result.graph_reused);

    // ── 12. Save state for next frame ─────────────────────────────────────
    if (sw_renderer) {
        commit_frame_state(
            sw_renderer, frame, ctx.frame_input.camera_2_5d,
            std::move(graph_result.compiled), exec_result.fb,
            resolved, frame_fp, dirty_out, width, height);
    }

    return exec_result.fb;
}

} // namespace chronon3d::graph
