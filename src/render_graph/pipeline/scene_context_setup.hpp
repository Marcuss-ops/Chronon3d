// ---------------------------------------------------------------------------
// scene_context_setup.hpp
//
// Internal-only header for the Phase 0 context-setup helper extracted
// from scene.cpp (Azione 19).  This header is NOT exposed to the SDK
// surface (does not live under include/chronon3d/) and is consumed
// exclusively by scene.cpp (caller) and scene_context_setup.cpp (impl).
//
// Counterpart header: scene_reuse_coordinator.hpp.
// ---------------------------------------------------------------------------

#pragma once

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "scene_fingerprint.hpp"   // src-only header (Check 17 architecture-boundary)
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::graph::detail {

// Assumes `ctx` is already constructed via make_graph_context().
// Mutates the following in-place:
//   * ctx.frame_input.assets_root
//   * ctx.frame_input.light_context, camera_2_5d, has_camera_2_5d, projection_ctx
//   * ctx.policy.debug_config  (when an actual SoftwareRenderer is wired)
//   * ctx.services.sw_renderer_sidecar  (threading from upstream callers)
//   * ctx.services.compiled_graph_cache / node_catalog / effect_catalog
//   * ctx.services.scheduler / session
//
// `sw_sidecar` is the optional SoftwareRenderer pointer propagated by upstream
// orchestrators (render_composition_frame, SoftwareRenderer::render, etc.).
//
// Returns the SoftwareRenderer* derived from ctx.services.sw_renderer_sidecar
// AFTER the catalog/scheduler/session wiring block.  May return nullptr
// when no SoftwareRenderer is wired (test paths, non-software backends).
//
// Pre-existing tag: PR 6.2 (root ExecutionScope per render invocation),
// TICKET-007 (single per-instance DebugConfig seeding), PR-B / PR-5 / PR-9.
SoftwareRenderer* setup_render_graph_context(
    RenderGraphContext& ctx,
    const Scene& scene,
    SoftwareRenderer* sw_sidecar);

} // namespace chronon3d::graph::detail
