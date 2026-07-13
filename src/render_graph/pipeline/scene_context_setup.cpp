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
// Counterpart: scene_reuse_coordinator.cpp (Phases 1+2+3).
// ---------------------------------------------------------------------------

#include "scene_context_setup.hpp"
#include "camera_change_policy.hpp"  // chronon3d::graph::detail::camera_changed used by Phase 2

#include <cassert>

namespace chronon3d::graph::detail {

SoftwareRenderer* setup_render_graph_context(
    RenderGraphContext& ctx,
    const Scene& scene,
    SoftwareRenderer* sw_sidecar)
{
    // ── Thread SoftwareRenderer sidecar from upstream callers ─────────
    // Source: render_composition_frame, SoftwareRenderer::render, etc.
    // Downstream phases (fingerprints, dirty rects, tile execution)
    // reach per-instance state via ctx.services.sw_renderer_sidecar
    // — no global lookup needed.
    if (sw_sidecar) {
        ctx.services.sw_renderer_sidecar = sw_sidecar;
    }

    SoftwareRenderer* sw_renderer =
        static_cast<SoftwareRenderer*>(ctx.services.sw_renderer_sidecar);

    // ── Per-composition assets root ───────────────────────────────────
    // Deep code uses ctx.resolve_asset() or ctx.frame_input.assets_root
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

    // ── TICKET-007 - single per-instance DebugConfig seeding ───────────
    // Every code path that reads `ctx.policy.debug_config`
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

    // ── Catalog + scheduler + session wiring ──────────────────────────
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
        // PR-9 — populate software sidecar so nodes can reach
        // cpu-specific resources (buffer_ring) via static_cast.
        ctx.services.sw_renderer_sidecar = sw_renderer;
        // PR-1 — production paths must have a wired scheduler after
        // pipeline wiring.  Test paths (no SoftwareRenderer) create
        // a local Sequential(1) scheduler.
        assert(ctx.services.scheduler != nullptr);
    }

    return sw_renderer;
}

} // namespace chronon3d::graph::detail
