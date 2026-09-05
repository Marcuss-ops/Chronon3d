// ---------------------------------------------------------------------------
// scene.cpp — Thin orchestrator for render_scene_via_graph() (Azione 19)
//
// Phase map (extracted to focused TUs):
//   0  scene_context_setup.cpp       — context + assets + camera + DebugConfig
//   1-3 tile_execution_policy.cpp    — canonical reuse plan + fingerprints
//   4-6  scene_dirty.cpp             — dirty rect + metrics
//   7   tile_execution_policy.cpp    — empty dirty-rect reuse in plan
//   8   graph_cache_coordinator.cpp  — build or reuse compiled graph
//   9   tile_execution_coordinator.cpp — tile decision + execute + fb
//   10  pool_preallocation.cpp       — preallocation
//   11  frame_timing_recorder.cpp    — phase timing telemetry
//   12  frame_state_commit.cpp       — commit state for next frame
//   n   scene_native_output.cpp      — native output sync + encode residency
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
#include <chronon3d/core/tracing/tracing_categories.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>

#include <chrono>

#include "helpers.hpp"
#include "scene_internal.hpp"
#include "scene_context_setup.hpp"        // Azione 19 — Phase 0 helper
#include "scene_native_output.hpp"        // native output sync + residency
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include "scene_fingerprint.hpp"
#include "tile_execution_policy.hpp"
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
#include <cstring>
#include <vector>
#include "temporal_render_context.hpp"
#include "../nodes/native_surface.hpp"

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_scene_via_graph_temporal(
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
    chronon3d::SoftwareRenderer* sw_sidecar,
    const TemporalRenderContext* temporal_context
) {
    CHRONON_TRACE_SCOPE("chronon.graph", "SceneTemporal");
    const auto t0 = profiling::now();
    cache::NodeCache* active_node_cache = temporal_context && temporal_context->value_cache
        ? temporal_context->value_cache
        : &node_cache;
    const bool isolated_temporal_sample = temporal_context != nullptr;
    RenderSettings sample_settings;
    if (isolated_temporal_sample) {
        sample_settings = settings;
        sample_settings.dirty.enabled = false;
        sample_settings.dirty.use_bitmask = false;
        sample_settings.dirty.use_tiles = false;
        sample_settings.dirty.tile_size = 0;
        sample_settings.diagnostics.enabled = false;
        sample_settings.diagnostics.plan = false;
        sample_settings.diagnostics.plan_output.clear();
        sample_settings.text_layout_debug = false;
        sample_settings.diagnostic_overlay_only = false;
    }
    const RenderSettings& effective_settings = isolated_temporal_sample
        ? sample_settings : settings;
    const auto hits_before = active_node_cache->stats().hits;

    // ── 0. Context setup ──
    auto ctx = make_graph_context(
        backend, *active_node_cache, camera, width, height, frame, frame_time,
        effective_settings, registry, video_decoder, fps,
        temporal_context ? temporal_context->sample_key : TemporalSampleKey{});
    SoftwareRenderer* sw_renderer =
        detail::setup_render_graph_context(ctx, scene, sw_sidecar);
    if (!ctx.node_exec.counters && sw_renderer && sw_renderer->counters()) {
        ctx.node_exec.counters = sw_renderer->counters();
    }
    if (sw_renderer) {
        ctx.services.surface_registry = &sw_renderer->runtime().surface_registry();
    }

    if (isolated_temporal_sample) {
        // Temporal samples get their own session/cache domains. The renderer
        // sidecar is still borrowed for immutable runtime services (backend,
        // catalogs, processor snapshot), but no sample state is published to
        // the main session.
        ctx.services.node_cache = active_node_cache;
        ctx.services.compiled_graph_cache = temporal_context->topology_cache;
        ctx.services.session = temporal_context->session;
        ctx.services.sw_renderer_sidecar = nullptr;
        ctx.frame_input.sample_time = temporal_context->sample_time;
        ctx.frame_input.temporal_key = temporal_context->sample_key;
        ctx.frame_input.time_seconds = static_cast<float>(
            temporal_context->sample_time.seconds());
        ctx.frame_input.fps = static_cast<float>(
            temporal_context->sample_time.fps());
        if (temporal_context->counters) {
            ctx.node_exec.counters = temporal_context->counters;
        } else {
            ctx.node_exec.counters = nullptr;
        }
        if (temporal_context->framebuffer_pool) {
            ctx.services.framebuffer_pool = temporal_context->framebuffer_pool;
        }
    }

    profiling::ProfilingGuard profiling_guard(
        ctx.node_exec.counters, ctx.services.framebuffer_pool.get());

    // The SDK contract is fail-closed for unresolved assets. Run the same
    // graph preflight against the owning runtime resolver before any backend
    // can substitute a diagnostic placeholder for a missing image.
    if (effective_settings.fail_on_missing_assets) {
        auto report = debug_preflight_render_graph(
            backend, *active_node_cache, scene, camera, width, height,
            frame, frame_time, effective_settings, registry, video_decoder,
            static_cast<float>(fps),
            sw_renderer ? &sw_renderer->runtime().resolver() : nullptr);
        if (report.has_warning_containing("MISSING_ASSET") ||
            report.has_warning_containing("ASSET_RESOLVER_UNAVAILABLE")) {
            spdlog::error("[graph-preflight] render rejected because an asset "
                          "could not be resolved");
            return nullptr;
        }
    }

    // Keep the native encode destination in the same Vulkan submission as the
    // graph work. This also covers the early framebuffer-reuse exits below:
    // they start a minimal batch containing only the final device-to-device
    // blit instead of falling back to a writer-side Vulkan submit.
    detail::NativeSourceResidency native_residency(ctx, backend);
    native_residency.begin_encode_batch();

    // ── 1-3. Canonical early execution-plan resolution ──
    FrameExecutionPlan execution_plan;
    if (!isolated_temporal_sample && !ctx.policy.require_native_gpu) {
        execution_plan = ExecutionResolver::resolve_early_reuse(
            ctx, scene, frame, static_cast<int>(width),
            static_cast<int>(height), sw_renderer);
        if (execution_plan.reuse_surface) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->fast_path_reused_frames.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->graph_skipped_frames.fetch_add(1, std::memory_order_relaxed);
            }
            return native_residency.finish_reused_native_frame(
                execution_plan.reuse_surface, frame);
        }
    }

    // ── 4. Graph-structure hint + diagnostics plan ──
    // Inform graph_cache_coordinator + dirty-paths whether topology is
    // unchanged so full rebuild is skipped.  PR-A removed the
    // ExecutionPlan cache that used to gate on this inside GraphExecutor;
    // the flag survives for the downstream coordinator (audit §9.4).
    ctx.policy.graph_structure_unchanged =
        execution_plan.scene_structure_unchanged &&
        !execution_plan.static_camera_changed;
    // DOF changes post-composite payload and depth-tracking execution state.
    // Until cached-graph refresh updates that state atomically, rebuild this
    // graph path to preserve warm/cold byte determinism.
    if (ctx.frame_input.has_camera_2_5d && ctx.frame_input.camera_2_5d.dof.enabled) {
        ctx.policy.graph_structure_unchanged = false;
    }

    if (ctx.policy.diagnostics_enabled && !isolated_temporal_sample) {
        const auto& history = sw_renderer->frame_history();
        const char* decision = execution_plan.reuse_surface
            ? "early_reuse"
            : (ctx.policy.graph_structure_unchanged
                ? "refresh_cached_candidate"
                : "build_fresh_candidate");
        spdlog::info(
            "[graph-frame-diagnostic] frame={} static_fp={} active_at_fp={} "
            "structure_fp={} combined_fp={} cached_static_fp={} "
            "cached_active_at_fp={} cached_structure_fp={} decision={}",
            static_cast<int>(frame), execution_plan.frame_fingerprints.static_fp,
            execution_plan.frame_fingerprints.active_at_fp,
            execution_plan.frame_fingerprints.structure_fp,
            execution_plan.frame_fingerprints.combined_fp,
            history.prev_static_scene_fingerprint,
            history.prev_active_at_fingerprint,
            history.prev_graph_structure_fingerprint, decision);
    }

    if (effective_settings.diagnostics.plan) {
        profiling::ProfilingGuard diag_guard(nullptr, profiling::g_current_framebuffer_pool);
        auto report = debug_preflight_render_graph(
            backend, *active_node_cache, scene, camera, width, height,
            frame, frame_time, effective_settings, registry, video_decoder,
            static_cast<float>(fps),
            sw_renderer ? &sw_renderer->runtime().resolver() : nullptr);
        spdlog::info("[graph-preflight] label='{}' frame={} size={}x{}\n{}",
                     diagnostic_label, static_cast<int>(frame), width, height, report.to_text());
        if (!settings.diagnostics.plan_output.empty()) {
            const auto report_path =            format_plan_output_path(
                effective_settings.diagnostics.plan_output, frame);

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
    auto dirty_out = isolated_temporal_sample
        ? detail::DirtyRectOutput{
            .layer_bboxes = {},
            .dirty_rect = raster::BBox{0, 0, width, height},
            .use_dirty_rects = false,
            .tile_grid = std::nullopt,
            .dirty_tiles = std::nullopt,
            .use_dirty_tiles = false}
        : detail::compute_dirty_rect(
            ctx, resolved, scene, settings, sw_renderer, frame, width, height);
    const auto t_dirty1 = profiling::now();

    // ── 6. Dirty metrics ──
    const double dirty_ratio = compute_and_apply_dirty_metrics(
        dirty_out, width, height, ctx.node_exec.counters,
        isolated_temporal_sample ? nullptr : sw_renderer);
    log_dirty_debug(isolated_temporal_sample ? nullptr : sw_renderer,
                    ctx.policy.diagnostics_enabled, dirty_out, frame);
    ctx.node_exec.dirty_rect = dirty_out.dirty_rect;

    // A projected surface must be rendered with the complete source-space
    // clip contract. Dirty/tile clipping can truncate a moving text card even
    // when the framebuffer itself is fully redrawn, producing intermittent
    // partial words. Keep 2.5D/native-3D scenes on the full-frame path until
    // projected polygon clipping is available end-to-end.
    native_residency.has_projected_surface = std::any_of(
        resolved.layers.begin(), resolved.layers.end(),
        [frame](const ResolvedLayer& layer) {
            return layer.layer && layer.layer->active_at(frame) &&
                   (layer.layer->uses_2_5d_projection || layer.layer->is_native_3d());
        });
    native_residency.set_upload_clip(
        native_residency.has_projected_surface
            ? std::optional<raster::BBox>(raster::BBox{0, 0, width, height})
            : dirty_out.dirty_rect);

    // ── 7. Complete canonical execution plan ──
    if (!isolated_temporal_sample) {
        CHRONON_TRACE_SCOPE("chronon.frame", "execution_plan_resolve");
        const auto output_format = (ctx.policy.native_video_encode_surface != runtime::kInvalidRenderSurfaceHandle &&
                                    ctx.services.surface_registry &&
                                    ctx.services.surface_registry->lookup(ctx.policy.native_video_encode_surface))
            ? ctx.services.surface_registry->lookup(ctx.policy.native_video_encode_surface)->desc.format.pixel
            : runtime::PixelFormat::Rgba32Float;

        execution_plan = ExecutionResolver::resolve(
            std::move(execution_plan), resolved, scene,
            ctx.frame_input.camera_2_5d, effective_settings, dirty_out,
            dirty_ratio, sw_renderer, frame, width, height,
            ctx.services.effect_catalog,
            ctx.policy.native_video_encode_surface !=
                runtime::kInvalidRenderSurfaceHandle,
            ctx.policy.diagnostics_enabled,
            output_format);
        ctx.policy.reuse_prev_framebuffer = execution_plan.use_dirty_region;
        ctx.policy.dirty_rects_enabled = execution_plan.use_dirty_region;
        ctx.policy.tile_execution_enabled =
            execution_plan.path == FrameExecutionPath::SparseTiles;
        if (execution_plan.force_full_frame_clear) {
            ctx.policy.skip_initial_clear = false;
            ctx.node_exec.clip_rect.reset();
            ctx.node_exec.dirty_rect.reset();
        }
        if (execution_plan.reuse_surface && !ctx.policy.require_native_gpu) {
            sw_renderer->commit_prev_frame_state(
                frame, resolved.camera.camera,
                execution_plan.frame_fingerprints.combined_fp,
                execution_plan.frame_fingerprints.static_fp,
                execution_plan.frame_fingerprints.structure_fp,
                execution_plan.frame_fingerprints.active_at_fp,
                std::move(dirty_out.layer_bboxes));
            return native_residency.finish_reused_native_frame(
                execution_plan.reuse_surface, frame);
        }
    }

    // ── 8. Ping-pong framebuffers + scratch (must precede build for ALL paths) ──
    if (sw_renderer) {
        if (!isolated_temporal_sample && sw_renderer->config().scheduler().pingpong_framebuffer()) {
            setup_pingpong_buffers(sw_renderer, width, height);
            ctx.node_exec.ping_write = sw_renderer->buffer_ring().write_slot_view();
        }
        ctx.node_exec.transform_scratch = isolated_temporal_sample
            ? temporal_context->scratch->slot_view(width, height)
            : sw_renderer->scratch_buffer().slot_view(width, height);
    }

    // ── 9. Build or reuse compiled graph ──
    const auto t_graph0 = profiling::now();
    auto graph_result = build_or_reuse_graph(
        ctx, scene, resolved, width, height,
        ctx.policy.graph_structure_unchanged,
        ctx.policy.diagnostics_enabled);
    if (dirty_out.frame_delta) {
        graph_result.compiled.execution_decision =
            ExecutionResolver::resolve_initial(*dirty_out.frame_delta);
    }
    ctx.policy.skip_initial_clear = graph_result.skip_initial_clear;
    const auto t_graph1 = profiling::now();

    // ── 9a. Capture TextRunNode snapshots (chronon3d_cli inspect-text) ──
    // Snapshot while graph is fully built and frozen, before execution
    // mutates any state.  Real TextRunShape + world matrix + predicted bbox.
    if (sw_renderer && !isolated_temporal_sample) {
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
    if (ctx.services.framebuffer_pool) {
        const size_t prealloc_count = preallocate_for_frame(
            *ctx.services.framebuffer_pool, graph_result.compiled,
            width, height, ctx.node_exec.counters,
            ctx.policy.diagnostics_enabled && !isolated_temporal_sample);
        (void)prealloc_count;
    }
    const auto t_prealloc1 = profiling::now();

    // ── 11. Execute: tile-based (V1) or traditional single-pass ──
    // PR 6.2 — root ExecutionScope per render invocation.  Binds the
    // session + compiled-graph identity for the entire execute phase so
    // child scopes (tile / precomp) can walk the parent chain.  Arena
    // reset happens inside execute_tile_or_fallback at scope exit.
    RenderSession fallback_session;   // for !sw_renderer test paths
    auto& session_ref = isolated_temporal_sample && temporal_context->session
        ? static_cast<RenderSession&>(*temporal_context->session)
        : (sw_renderer
            ? static_cast<RenderSession&>(sw_renderer->session())
            : fallback_session);
    ExecutionScope root_scope = ExecutionScope::make_root(
        session_ref, session_ref.arena(),
        graph_result.compiled.graph_instance_id);
    const auto t_exec0 = profiling::now();
    // Frame-batching boundary: a batching backend records every graph pass
    // between these calls and performs a single submission in
    // end_frame_batch().  No-op for backends without batching support.
    native_residency.begin_frame_batch();
    auto exec_result = execute_tile_or_fallback(
        ctx, graph_result.compiled,        resolved, effective_settings, dirty_out,
        execution_plan,
        dirty_ratio, isolated_temporal_sample ? nullptr : sw_renderer,
        frame, width, height, root_scope);
    exec_result.fb = native_residency.finish_frame_encode(
        std::move(exec_result.fb), frame);
    // Native surfaces remain authoritative across composite nodes. The public
    // render API still returns a CPU Framebuffer, so perform exactly one
    // terminal synchronization here rather than between every graph pass.
    detail::synchronize_native_output(ctx, exec_result.fb);
    backend.retire_frame_transient_surfaces();
    const auto t_exec1 = profiling::now();

    if (sw_renderer && !isolated_temporal_sample) {
        sw_renderer->dirty_telemetry().record_execution_cost(
            exec_result.use_tile_execution,
            profiling::duration_ms(t_exec0, t_exec1));
    }

    // ── 12. Phase timing telemetry ──
    compute_and_record_timings(
        t_resolve0, t_resolve1, t_dirty0, t_dirty1,
        t_graph0, t_graph1, t_exec0, t_exec1,
        ctx.node_exec.counters,        ctx.policy.diagnostics_enabled && !isolated_temporal_sample,
        frame, graph_result.graph_reused);
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->graph_executed_frames.fetch_add(1, std::memory_order_relaxed);
        if (graph_result.graph_reused) {
            ctx.node_exec.counters->graph_reused_frames.fetch_add(1, std::memory_order_relaxed);
        }
    }
    record_dirty_telemetry(isolated_temporal_sample ? nullptr : sw_renderer, dirty_out,
        exec_result.use_tile_execution, graph_result.graph_reused,
        execution_plan.path);

    // ── 13. Save state for next frame ──
    if (sw_renderer && !isolated_temporal_sample) {
        commit_frame_state(
            sw_renderer, frame, ctx.frame_input.camera_2_5d,
            std::move(graph_result.compiled), exec_result.fb,
            resolved, execution_plan.frame_fingerprints, dirty_out, width, height);
    }

    return exec_result.fb;
}

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
    chronon3d::SoftwareRenderer* sw_sidecar)
{
    return render_scene_via_graph_temporal(
        backend, node_cache, scene, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps, diagnostic_label, sw_sidecar,
        nullptr);
}

} // namespace chronon3d::graph
