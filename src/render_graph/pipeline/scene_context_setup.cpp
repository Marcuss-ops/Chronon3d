// ---------------------------------------------------------------------------
// scene_context_setup.cpp
//
// Implementation of detail::setup_render_graph_context() — Phase 0 of
// render_scene_via_graph() extracted from scene.cpp (Azione 19).
//
// Handles:
//   * assets_root threading
//   * light_context
//   * camera + projection_ctx (when resolved_camera.camera.enabled)
//   * DebugConfig seeding (per-engine; TICKET-007)
//   * scheduler / session / catalog wiring
//   * SoftwareRenderer sidecar threading from upstream callers
//
// Counterpart: tile_execution_policy.cpp (canonical reuse-plan resolution).
// ---------------------------------------------------------------------------

#include "scene_context_setup.hpp"
#include "camera_change_policy.hpp"  // chronon3d::graph::detail::camera_changed used by Phase 2
#include <chronon3d/backends/software/render_settings.hpp>
#include "helpers.hpp"               // resolve_scene_camera, build_graph_context, make_default_pipeline_flags
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>

#include <cassert>

namespace chronon3d::graph::detail {

SoftwareRenderer* setup_render_graph_context(
    RenderGraphContext& ctx,
    const Scene& scene,
    SoftwareRenderer* sw_sidecar)
{
    // ── Thread the narrow software frame resource ─────────────────────
    // Source: render_composition_frame, SoftwareRenderer::render, etc.
    // Graph nodes only need the buffer ring for framebuffer reuse; keeping
    // that typed dependency on the node workspace avoids a renderer
    // service-locator/opaque-sidecar escape hatch.
    if (sw_sidecar) {
        ctx.node_exec.buffer_ring = &sw_sidecar->buffer_ring();
        if (!ctx.node_exec.counters && sw_sidecar->counters()) {
            ctx.node_exec.counters = sw_sidecar->counters();
        }
    }

    SoftwareRenderer* sw_renderer = sw_sidecar;

    // A prepared job supplies the active frame lease. Prefer its ownership
    // chain for execution-local routing; standalone graph diagnostics still
    // use the legacy explicit pointers below.
    if (ctx.execution) {
        auto& job = ctx.execution->job();
        ctx.runtime = &job.runtime();
        ctx.session = &job;
        if (sw_sidecar) ctx.program_store = &sw_sidecar->program_store();
        ctx.scheduler = &job.runtime().scheduler();
        ctx.compiled_graph_cache = &job.runtime().graph_cache();
        ctx.node_cache = &job.runtime().node_cache();
        ctx.framebuffer_pool = job.runtime().framebuffer_pool_shared();
    }

    // ── Runtime assets root ──────────────────────────────────────────
    // Deep code uses ctx.resolve_asset() or ctx.frame_input.assets_root().
    // Asset roots are owned by the render runtime, not by the scene, and are
    // written here once per setup (never per frame), so they live in the
    // shared immutable FrameStaticContext: per-node context clones copy a
    // shared_ptr instead of deep-copying the string (hot-path tax C).
    if (sw_renderer) {
        ctx.runtime = &sw_renderer->runtime();
        ctx.asset_resolver = &sw_renderer->runtime().resolver();
        if (const auto root = sw_renderer->runtime().resolver().mount_root();
            !root.empty()) {
            ctx.frame_input.static_context =
                std::make_shared<const graph::FrameStaticContext>(
                    graph::FrameStaticContext{root.string()});
        }
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

    // ── TICKET-007 - single per-instance DebugConfig seeding ───────────
    // Every code path that reads `ctx.policy.debug_config`
    // (GlowPipeline::render, future text-bbox overlay relay, etc.)
    // now reflects the OWNING engine's debug flags instead of the
    // previously-removed process-wide `detail::g_debug_config`.
    // When the backend is a non-SoftwareRenderer render backend,
    // debug_cfg remains nullptr and overlays are skipped -
    // the safe default for non-software backends and matches the
    // pre-existing test contract for the modern TextRun path (the
    // retired tests/test_text_preset_registry.cpp suite).  // drift-class: historical (test retired in text-preset consolidation)
    if (sw_renderer) {
        ctx.policy.debug_config = &sw_renderer->config().debug();
    }

    // ── Catalog + scheduler + session wiring ──────────────────────────
    // Wire compiled_graph_cache + effect_catalog + scheduler
    // into the render graph context so that graph_cache_coordinator,
    // PrecompNode creation, and dirty/tile policies can access them
    // without a SoftwareRenderer dependency.
    if (sw_renderer) {
        if (!ctx.execution) ctx.compiled_graph_cache = &sw_renderer->graph_cache();
        // The processor snapshot captured by SoftwareBackend is owned by
        // SoftwareRegistry and carries that registry's generation. Use the
        // same canonical generation for graph compilation; hashing it with
        // the separate pipeline-catalog generation would make every valid
        // snapshot look stale and reject cacheable graphs before execution.
        ctx.registry_generation = sw_renderer->software_registry().generation();
        ctx.node_exec.processor_snapshot = sw_renderer->backend().processor_snapshot();
        ctx.processor_snapshot_required = sw_renderer->backend().requires_processor_snapshot();
        // Pipeline catalogs are frozen for the runtime lifetime; their
        // generation remains part of the runtime-owned topology policy and
        // must not be mixed into the processor-snapshot generation contract.
        ctx.effect_catalog = &sw_renderer->effect_catalog();
        // ── PR-B: propagate scheduler to nested graph call sites ──────
        // PrecompNode dereferences `*ctx.scheduler` to route its
        // inner execute() through the same arena as the parent graph.
        if (!ctx.execution) ctx.scheduler = &sw_renderer->scheduler();
        // PR-5 — propagate the job-session pointer so PrecompNode borrows
        // the scene program store through the typed JobSession seam.
        if (!ctx.execution) ctx.session = &sw_renderer->session();
        if (!ctx.execution) ctx.program_store = &sw_renderer->program_store();
        ctx.text_render_resources = sw_renderer->text_render_resources();
        // PR-1 — production paths must have a wired scheduler after
        // pipeline wiring.  Test paths (no SoftwareRenderer) create
        // a local Sequential(1) scheduler.
        assert(ctx.scheduler != nullptr);
    }

    return sw_renderer;
}

} // namespace chronon3d::graph::detail
