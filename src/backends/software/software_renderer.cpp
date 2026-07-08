// ===========================================================================
// src/backends/software/software_renderer.cpp
//
// M1.5#12 main — SoftwareRenderer thin orchestrator (lifecycle + dispatch
// surface).  Long-lived state lives on `m_runtime` (chronon3d::runtime::
// RenderRuntime); this TU holds ONLY the bits that MUST stay in one place:
//
//   * Move ops (SoftwareRenderer&& / operator=) — destructor timing
//     coordination across ImageRenderer, ImageBackend, FontEngine,
//     TextRenderResources, SoftwareRegistry, SoftwareRenderSession.
//     ODR: defined in EXACTLY one TU (here).  See notes below.
//
//   * Dtor (= default).
//
//   * Settings/diagnostics setters (set_settings / set_motion_blur /
//     set_diagnostic_mode / reset_counters / set_image_backend /
//     set_video_decoder / set_composition_registry) — only one of
//     (set_video_decoder / set_composition_registry / clear_node_cache)
//     are inline-bodied in the header; the multi-line bodies live here.
//
//   * Cache ops (clear_caches) — touches multiple subsystems
//     (image_renderer + HAS_BACKEND_TEXT effect caches + node_cache +
//     framebuffer_pool + graph_cache + m_session.reset_job).
//
//   * Graph-pipeline orchestration (mark_fast_path_reused /
//     commit_frame_state / commit_prev_frame_state /
//     update_dirty_telemetry) — all session.frame_history / dirty_telemetry
//     writers; consolidated to keep the per-session invariant at one place.
//
//   * 14 RenderRuntime forwarders (software_registry / graph_node_registry /
//     effect_catalog / software_framebuffer_pool / framebuffer_pool /
//     scheduler / runtime / graph_cache / backend / node_cache + font_engine
//     CHRONON3D_ENABLE_TEXT path) — moved OOL from the header to keep
//     header LOC under the gate-bound (06 R3b).
//
// What's NOT here anymore (M1.5#12 split — 3 companion TUs):
//   * 2 regular ctors              → software_renderer_factory.cpp
//   * render / render_scene × 2    → software_renderer_dispatch.cpp +
//                                     software_renderer_text.cpp
//   * debug_render_graph           → software_renderer_dispatch.cpp
//   * draw_node                    → software_renderer_dispatch.cpp
//   * composite_layer / apply_blur → software_renderer_dispatch.cpp
//   * apply_per_pixel_dof          → software_renderer_dispatch.cpp
//   * apply_effect_stack           → software_renderer_effects.cpp
//   * preflight_fonts              → software_renderer_text.cpp
//   * text_render_resources() × 2  → software_renderer_text.cpp
//   * font_engine() × 2            → software_renderer_text.cpp
//   * to_local_clip / clipped_area → software_renderer_dispatch.cpp
//   * RenderIOFenceGuard (RAII)    → software_renderer_text.cpp
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
// Required for `m_software_registry` (unique_ptr<renderer::SoftwareRegistry>)
// deleter instantiation in the move ops below — without this the compiler
// raises "incomplete type" on the unique_ptr dtor path.
#include <chronon3d/backends/software/software_registry.hpp>
// Required by the renderer-namespace forward decls of `apply_color_effects`
// (uses `LayerEffect`); pre-split incarnation pulled this in transitively
// via `utils/render_effects_processor.hpp`, which we no longer include.
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#ifdef CHRONON3D_HAS_BACKEND_TEXT
// 06 R5b — text-effect cache clear functions (clear_text_glow_cache /
// clear_text_shadow_cache) live exclusively in the text-effect TU.  Pulling
// the software_text_effects.hpp header here keeps `clear_caches()`'s
// HAS_BACKEND_TEXT block self-contained without polluting the SDK header.
// NOTE: software_text_effects.hpp transitively brings in
// `chronon3d::renderer::apply_blur / apply_color_effects / apply_effect_stack`
// (software-text-effects dispatches into the renderer's effect stack); the
// explicit forward-decl block previously in this TU was redundant and has
// been removed (code-review round-2 nits).
#include "processors/text/software_text_effects.hpp"
#endif
#include <chronon3d/scene/model/render/render_node.hpp>
#include <utility>

namespace chronon3d {

// ── Lifecycle — move ops + dtor (single TU per ODR) ──────────────────────────
//
// Move ops use real rvalue-ref (no EAST-CONST hack). See header.
// Members are std::move'd into *this; the moved-from object is left
// destructible-but-empty (raw pointers nulled; unique_ptrs emptied) to
// avoid double-destruction of nested resources (m_image_renderer,
// m_session each carry shared internal mutex / pool state).
//
// ODR INVARIANT: these definitions live in EXACTLY this TU.  Adding a
// second definition (in factory/text/dispatch/effects) would cause an
// unspecified-behavior linker error (duplicate symbol).  Any future
// split that touches move ops MUST either move ALL 3 here or move
// ALL 3 OUT of here in one atomic commit.
SoftwareRenderer::SoftwareRenderer(SoftwareRenderer&& other) noexcept
    : m_config(std::move(other.m_config))
    , m_settings(std::move(other.m_settings))
    , m_counters(std::move(other.m_counters))
    , m_image_renderer(std::move(other.m_image_renderer))
    , m_video_decoder(std::move(other.m_video_decoder))
    , m_image_backend(std::move(other.m_image_backend))
    , m_registry(other.m_registry)
    , m_runtime(other.m_runtime)
    , m_owned_runtime_storage(
          std::move(other.m_owned_runtime_storage))
#ifdef CHRONON3D_ENABLE_TEXT
    , m_font_engine(
          std::move(other.m_font_engine))
    , m_text_render_resources(
          std::move(other.m_text_render_resources))
#endif
    , m_software_registry(
          std::move(other.m_software_registry))
    , m_session(std::move(other.m_session))
{
    m_runtime = nullptr;
    m_registry = nullptr;
}

SoftwareRenderer&
SoftwareRenderer::operator=(SoftwareRenderer&& other) noexcept
{
    m_config        = std::move(other.m_config);
    m_settings      = std::move(other.m_settings);
    m_counters      = std::move(other.m_counters);
    m_image_renderer = std::move(other.m_image_renderer);
    m_video_decoder  = std::move(other.m_video_decoder);
    m_image_backend  = std::move(other.m_image_backend);
    m_registry       = other.m_registry;
    m_runtime        = other.m_runtime;
    m_owned_runtime_storage = std::move(other.m_owned_runtime_storage);
#ifdef CHRONON3D_ENABLE_TEXT
    m_font_engine    = std::move(other.m_font_engine);
    m_text_render_resources = std::move(other.m_text_render_resources);
#endif
    m_software_registry = std::move(other.m_software_registry);
    m_session        = std::move(other.m_session);
    other.m_runtime = nullptr;
    other.m_registry = nullptr;
    return *this;
}

SoftwareRenderer::~SoftwareRenderer() = default;

// ── Settings + diagnostics setters (multi-line bodies) ───────────────────────
//
// 06 R3b — implementations moved OOL from the header to drop header LOC
// into the I2 ≤ 200 bound.
void SoftwareRenderer::set_settings(const RenderSettings& settings) {
    m_settings = settings;
    m_counters.program_cache_capacity.store(settings.program_cache_capacity, std::memory_order_relaxed);
    m_counters.program_cache_tune.store(settings.program_cache_tune ? 1 : 0, std::memory_order_relaxed);
}

void SoftwareRenderer::set_motion_blur(MotionBlurSettings mb) {
    m_settings.motion_blur = std::move(mb);
}

void SoftwareRenderer::set_diagnostic_mode(bool enabled) {
    m_settings.diagnostics.enabled = enabled;
}

void SoftwareRenderer::reset_counters() {
    m_counters.reset();
}

void SoftwareRenderer::clear_caches() {
    m_image_renderer.clear_cache();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    renderer::clear_text_glow_cache();
    renderer::clear_text_shadow_cache();
#endif
    node_cache().clear();
    if (auto* pool = m_runtime->framebuffer_pool_shared().get()) {
        pool->clear();
    }
    m_runtime->graph_cache() = {};
    m_session.reset_job();
}

void SoftwareRenderer::set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
    m_image_backend = std::move(backend);
    m_image_renderer.set_backend(m_image_backend);
}

// ── Graph-pipeline orchestration (per-session writers) ───────────────────────
//
// 06 R3b — multi-line implementations moved OOL so the header can stay
// under the 200-LOC bound.  All writers touch `m_session.common` which is
// the canonical per-renderer session state.  Grouped together (instead of
// scattered across dispatch/text/effects TUs) so the per-session invariant
// is enforced at one canonical site — easier to audit on session-telemetry
// regressions (TICKET-118 cross-references this invariant).
void SoftwareRenderer::mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp) {
    m_session.common.dirty_telemetry.last_dirty_area_ratio = 0.0;
    m_session.common.dirty_telemetry.last_dirty_rect_enabled = false;
    m_session.common.dirty_telemetry.last_dirty_rect = std::nullopt;
    m_session.common.dirty_telemetry.last_tile_execution_used = false;
    m_session.common.dirty_telemetry.last_fast_path_reused = true;
    m_session.common.dirty_telemetry.last_graph_reused = false;
    m_session.common.frame_history.prev_frame = frame;
    m_session.common.frame_history.prev_scene_fingerprint = combined_fp;
    m_session.common.frame_history.prev_camera = cam;
    m_session.common.frame_history.prev_camera_valid = cam.enabled;
}

void SoftwareRenderer::commit_frame_state(Frame frame, const Camera2_5D& cam,
                                          uint64_t combined_fp, uint64_t static_fp,
                                          uint64_t structure_fp, uint64_t active_at_fp,
                                          std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
    commit_prev_frame_state(frame, cam, combined_fp, static_fp, structure_fp, active_at_fp,
                            std::move(layer_bboxes));
}

void SoftwareRenderer::commit_prev_frame_state(Frame frame, const Camera2_5D& cam,
                                               uint64_t combined_fp, uint64_t static_fp,
                                               uint64_t structure_fp, uint64_t active_at_fp,
                                               std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
    m_session.common.dirty_telemetry.previous_layers = std::move(layer_bboxes);
    m_session.common.frame_history.prev_frame = frame;
    m_session.common.frame_history.prev_scene_fingerprint = combined_fp;
    m_session.common.frame_history.prev_static_scene_fingerprint = static_fp;
    m_session.common.frame_history.prev_graph_structure_fingerprint = structure_fp;
    m_session.common.frame_history.prev_active_at_fingerprint = active_at_fp;
    m_session.common.frame_history.prev_camera = cam;
    m_session.common.frame_history.prev_camera_valid = cam.enabled;
}

void SoftwareRenderer::update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                               bool tile_execution_used, bool fast_path_reused,
                                               bool graph_reused) {
    m_session.common.dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
    m_session.common.dirty_telemetry.last_dirty_rect = rect;
    m_session.common.dirty_telemetry.last_tile_execution_used = tile_execution_used;
    m_session.common.dirty_telemetry.last_fast_path_reused = fast_path_reused;
    m_session.common.dirty_telemetry.last_graph_reused = graph_reused;
}

// ── RenderRuntime forwarders (OOL — avoids pulling runtime/* headers) ────────
//
// 06 R3b — implementations moved out-of-line; their inline bodies would
// dereference `m_runtime->X()` / `*m_runtime` which require the complete
// `runtime::RenderRuntime` type.  Pulling
// `<chronon3d/runtime/render_runtime.hpp>` into the header would exceed
// the boundary-gate I3 ≤ 6-include budget, so the implementation lives
// here where the type is complete (this TU already pulls the header via
// `software_renderer.hpp`'s user clients).
//
// Note: font_engine() lives in `software_renderer_text.cpp` because the
// unique_ptr<FontEngine> deleter must be instantiated in EXACTLY one TU
// (WP-8 PR 8.1 — ODR violation risk if split across TUs).
renderer::SoftwareRegistry& SoftwareRenderer::software_registry()             { return *m_software_registry; }
const renderer::SoftwareRegistry& SoftwareRenderer::software_registry() const { return *m_software_registry; }
graph::GraphNodeCatalog& SoftwareRenderer::graph_node_registry()             { return m_runtime->graph_node_registry(); }
const graph::GraphNodeCatalog& SoftwareRenderer::graph_node_registry() const { return m_runtime->graph_node_registry(); }
effects::EffectCatalog& SoftwareRenderer::effect_catalog()                   { return m_runtime->effect_catalog(); }
const effects::EffectCatalog& SoftwareRenderer::effect_catalog() const       { return m_runtime->effect_catalog(); }
cache::FramebufferPool& SoftwareRenderer::software_framebuffer_pool()        { return m_runtime->framebuffer_pool(); }
const cache::FramebufferPool& SoftwareRenderer::software_framebuffer_pool() const { return m_runtime->framebuffer_pool(); }
std::shared_ptr<cache::FramebufferPool> SoftwareRenderer::framebuffer_pool() { return m_runtime->framebuffer_pool_shared(); }
// Note: executor forwarder was removed.  See commit message of the
// baseline-12c295be PR for the architectural rationale.
chronon3d::ExecutionScheduler& SoftwareRenderer::scheduler() noexcept        { return m_runtime->scheduler(); }
const chronon3d::ExecutionScheduler& SoftwareRenderer::scheduler() const noexcept { return m_runtime->scheduler(); }
runtime::RenderRuntime& SoftwareRenderer::runtime() noexcept                  { return *m_runtime; }
const runtime::RenderRuntime& SoftwareRenderer::runtime() const noexcept      { return *m_runtime; }
graph::CompiledGraphCache& SoftwareRenderer::graph_cache()                   { return m_runtime->graph_cache(); }
const graph::CompiledGraphCache& SoftwareRenderer::graph_cache() const       { return m_runtime->graph_cache(); }
graph::RenderBackend& SoftwareRenderer::backend()                             { return m_runtime->backend(); }
const graph::RenderBackend& SoftwareRenderer::backend() const                 { return m_runtime->backend(); }
cache::NodeCache& SoftwareRenderer::node_cache() noexcept                     { return m_runtime->node_cache(); }
const cache::NodeCache& SoftwareRenderer::node_cache() const noexcept         { return m_runtime->node_cache(); }
// font_engine() intentionally NOT here — see software_renderer_text.cpp.

} // namespace chronon3d
