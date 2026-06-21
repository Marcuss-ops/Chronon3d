#pragma once

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/media/frame_source_provider.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
// WP-3 PR 3.4 close-out: include the canonical SoftwareRenderSession
// definition directly.  The legacy duplicate struct that used to live
// in <chronon3d/runtime/render_session.hpp> has been removed to eliminate
// ODR risk (two struct definitions with identical members).
#include <chronon3d/backends/software/software_render_session.hpp>
#include <chronon3d/core/config.hpp>

#include <memory>
#include <optional>
#include <unordered_map>

// ----------------------------------------------------------------------
// backends/software/software_renderer.hpp
//
// TICKET-011 — SoftwareRenderer NO LONGER OWNS long-lived infrastructure.
//
// Previously, SoftwareRenderer held:
//   - `m_cache_state`    (NodeCache + FramebufferPool + CompiledGraphCache)
//   - `m_runtime_resources` (SoftwareRegistry + GraphExecutor +
//                            ExecutionPlanCache + GraphNodeCatalog +
//                            EffectCatalog)
//   - `m_backend`        (SoftwareBackend instance)
//
// All of these now live on `chronon3d::runtime::RenderRuntime`.  The
// renderer holds a `RenderRuntime*` pointer (set in the ctor init list)
// plus only its per-instance state — Config copy, RenderSettings,
// RenderCounters, ImageRenderer, SoftwareRenderSession, MediaFrameProvider,
// ImageBackend, CompositionRegistry pointer.
//
// NOTE on the pointer: `m_runtime` is a POINTER, not a reference,
// because the transitional `SoftwareRenderer(Config)` ctor synthesises
// an internal RenderRuntime via `m_owned_runtime_storage` and binds the
// pointer to it.  All callers therefore use `m_runtime->foo()`, never
// `m_runtime.foo()`.
// ----------------------------------------------------------------------

namespace chronon3d {

class SoftwareRenderer;
class CompositionRegistry;

namespace renderer {
void clear_text_glow_cache();
void clear_text_shadow_cache();
}

/**
 * SoftwareRenderer — per-instance rendering orchestrator.
 *
 * Borrows caches / executor / scheduler / catalogs / backend from a
 * RenderRuntime pointer passed at construction.  Holds per-instance
 * state: Config copy, RenderSettings, RenderCounters, ImageRenderer,
 * SoftwareRenderSession, MediaFrameProvider, ImageBackend,
 * CompositionRegistry pointer.
 */
class SoftwareRenderer : public Renderer, public graph::RenderBackend {
public:
    std::shared_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame);
    std::shared_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera,
                                              i32 width, i32 height);
    std::shared_ptr<Framebuffer> render_scene(const Scene& scene,
                                              const std::optional<Camera2_5D>& camera,
                                              i32 width, i32 height) override;
    [[nodiscard]] std::string debug_render_graph(const Scene& scene, const Camera& camera,
                                                 i32 width, i32 height, Frame frame = 0,
                                                 f32 frame_time = 0.0f) const;

    // ── Construction ────────────────────────────────────────────────
    /// Primary ctor — takes a wired RenderRuntime + engine Config.
    /// The renderer borrows caches/executor/scheduler/catalogs from the
    /// runtime; the backend is attached to the runtime externally
    /// (post-construction in RenderEngine::Impl).
    explicit SoftwareRenderer(runtime::RenderRuntime& rt, Config config);

    /// Test/CLI transitional ctor — synthesises an internal RenderRuntime.
    /// The synthesised runtime owns its own caches; the renderer borrows
    /// from it the same way as the primary ctor.
    explicit SoftwareRenderer(Config config);

    ~SoftwareRenderer() override;

    // Non-copyable.  Movable iff the RenderRuntime pointer can transfer;
    // in practice a SoftwareRenderer is always held via unique_ptr so move
    // is not relied upon.
    SoftwareRenderer(const SoftwareRenderer&) = delete;
    SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
    SoftwareRenderer(SoftwareRenderer&&) noexcept = default;
    SoftwareRenderer& operator=(SoftwareRenderer&&) noexcept = default;

    // ── Render settings management ───────────────────────────────────
    void set_settings(const RenderSettings& settings) {
        m_settings = settings;
        m_counters.program_cache_capacity.store(settings.program_cache_capacity, std::memory_order_relaxed);
        m_counters.program_cache_tune.store(settings.program_cache_tune ? 1 : 0, std::memory_order_relaxed);
    }
    [[nodiscard]] const RenderSettings& settings() const { return m_settings; };

    // Legacy/Compatibility setters (redirect to m_settings)
    void set_motion_blur(MotionBlurSettings mb) { m_settings.motion_blur = mb; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const { return m_settings.motion_blur; }

    void set_diagnostic_mode(bool enabled) { m_settings.diagnostics.enabled = enabled; }
    [[nodiscard]] bool is_diagnostic_mode() const { return m_settings.diagnostics.enabled; }

    // ── Intentional cache operations ──────────────────────────────────

    /// Clear all caches (image, font, node, pool, graph, frame state).
    /// Forwards to the runtime's NodeCache/FramebufferPool/CompiledGraphCache.
    /// WP-3 PR 3.4 close-out: the session reset invokes
    /// `SoftwareRenderSession::reset_job()` (the canonical full-reset
    /// path that collapses the legacy shim into the explicit reset
    /// APIs; both halves — `RenderSession` and `SoftwareSessionResources`
    /// — are reset).
    void clear_caches() {
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

    /// Clear only the node cache.
    void clear_node_cache() { node_cache().clear(); }

    /// Reset all profiling counters to zero.
    void reset_counters() { m_counters.reset(); }

    void set_composition_registry(const CompositionRegistry* registry) { m_registry = registry; }
    [[nodiscard]] const CompositionRegistry* composition_registry() const { return m_registry; }

    /// Apply per-pixel depth-of-field blur (RenderBackend contract).
    void apply_per_pixel_dof(
        Framebuffer& framebuffer,
        std::span<const float> depth,
        const DepthOfFieldSettings& dof,
        const LensModel& lens,
        const std::optional<raster::BBox>& clip) override;

    [[nodiscard]] ImageRenderer& image_renderer() { return m_image_renderer; }
    [[nodiscard]] cache::NodeCache& node_cache() noexcept { return m_runtime->node_cache(); }
    [[nodiscard]] const cache::NodeCache& node_cache() const noexcept { return m_runtime->node_cache(); }

    [[nodiscard]] const RenderSettings& render_settings() const { return m_settings; }

    void set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder) {
        m_video_decoder = std::move(decoder);
    }
    [[nodiscard]] media::MediaFrameProvider* video_decoder() const {
        return m_video_decoder.get();
    }

    void set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
        m_image_backend = std::move(backend);
        m_image_renderer.set_backend(m_image_backend);
    }
    [[nodiscard]] image::ImageBackend* image_backend() const {
        return m_image_backend.get();
    }

    // ── Dirty-rect telemetry (populated by render_scene_via_graph) ──────────
    [[nodiscard]] double last_dirty_area_ratio() const { return m_session.common.dirty_telemetry.last_dirty_area_ratio; }
    [[nodiscard]] bool last_dirty_rect_enabled() const { return m_session.common.dirty_telemetry.last_dirty_rect_enabled; }
    [[nodiscard]] std::optional<raster::BBox> last_dirty_rect() const { return m_session.common.dirty_telemetry.last_dirty_rect; }
    [[nodiscard]] bool last_tile_execution_used() const { return m_session.common.dirty_telemetry.last_tile_execution_used; }
    [[nodiscard]] bool last_fast_path_reused() const { return m_session.common.dirty_telemetry.last_fast_path_reused; }
    [[nodiscard]] bool last_graph_reused() const { return m_session.common.dirty_telemetry.last_graph_reused; }
    [[nodiscard]] int last_layer_count() const { return m_session.common.dirty_telemetry.last_layer_count; }

    // Public for use by graph nodes via RenderGraphContext.
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, i32 width, i32 height) override;
    void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt) override;
    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, const effects::EffectExecutionContext& context) override;
    void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip = std::nullopt, CompositeOperator op = CompositeOperator::SourceOver) override;

    [[nodiscard]] graph::RenderCapabilities capabilities() const noexcept override {
        return graph::RenderCapabilities{
#ifdef CHRONON3D_USE_BLEND2D
            .text_run = true
#else
            .text_run = false
#endif
        };
    }

    graph::RenderOpResult draw_text_run(Framebuffer& fb, const TextRunShape& shape, const Mat4& model_matrix,
                                        float opacity) override;

    // ── Forwarders to RenderRuntime (TICKET-011) ─────────────────────
    [[nodiscard]] renderer::SoftwareRegistry& software_registry() {
        return m_runtime->software_registry();
    }
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const {
        return m_runtime->software_registry();
    }

    [[nodiscard]] graph::GraphNodeCatalog& graph_node_registry() {
        return m_runtime->graph_node_registry();
    }
    [[nodiscard]] const graph::GraphNodeCatalog& graph_node_registry() const {
        return m_runtime->graph_node_registry();
    }

    [[nodiscard]] effects::EffectCatalog& effect_catalog() {
        return m_runtime->effect_catalog();
    }
    [[nodiscard]] const effects::EffectCatalog& effect_catalog() const {
        return m_runtime->effect_catalog();
    }

    std::shared_ptr<cache::FramebufferPool> framebuffer_pool() override {
        return m_runtime->framebuffer_pool_shared();
    }
    [[nodiscard]] cache::FramebufferPool& software_framebuffer_pool() {
        return m_runtime->framebuffer_pool();
    }
    [[nodiscard]] const cache::FramebufferPool& software_framebuffer_pool() const {
        return m_runtime->framebuffer_pool();
    }
    [[nodiscard]] RenderCounters* counters() override { return &m_counters; }
    [[nodiscard]] const RenderCounters* counters() const { return &m_counters; }

    [[nodiscard]] graph::GraphExecutor* executor() {
        return &m_runtime->executor();
    }
    [[nodiscard]] const graph::GraphExecutor* executor() const {
        return &m_runtime->executor();
    }

    // TICKET-011 / PR-B — accessor for the authoritative task-aren a
    // scheduler (owned by the runtime).  PrecompNode uses
    // `ctx.services.scheduler` → this accessor → `m_runtime->scheduler()`
    // to route nested-graph execute() calls through the same arena as
    // the parent graph.  Never nullptr while m_runtime is valid.
    [[nodiscard]] chronon3d::ExecutionScheduler& scheduler() noexcept {
        return m_runtime->scheduler();
    }
    [[nodiscard]] const chronon3d::ExecutionScheduler& scheduler() const noexcept {
        return m_runtime->scheduler();
    }


    [[nodiscard]] graph::CompiledGraphCache& graph_cache() { return m_runtime->graph_cache(); }
    [[nodiscard]] const graph::CompiledGraphCache& graph_cache() const { return m_runtime->graph_cache(); }

    /// Per-instance engine configuration (replaces legacy Config singleton).
    [[nodiscard]] const Config& config() const { return m_config; }
    [[nodiscard]] Config& config() { return m_config; }

    /// Access the underlying RenderBackend (lives on the runtime;
    /// attached externally by RenderEngine::Impl).  Throws via
    /// `m_runtime->backend()` if no backend has been attached.
    [[nodiscard]] graph::RenderBackend& backend() { return m_runtime->backend(); }
    [[nodiscard]] const graph::RenderBackend& backend() const { return m_runtime->backend(); }

    // ── RenderSession access ────────────────────────────────────────────
    [[nodiscard]] RenderSession& session() { return m_session.common; }
    [[nodiscard]] const RenderSession& session() const { return m_session.common; }

    [[nodiscard]] SoftwareRenderSession& software_session() { return m_session; }
    [[nodiscard]] const SoftwareRenderSession& software_session() const { return m_session; }

    // ── Convenience accessors (forward to session) ──────────────────────
    using LayerBBoxState          = ::chronon3d::LayerBBoxState;
    using RendererFrameHistory    = ::chronon3d::RendererFrameHistory;
    using RendererDirtyTelemetry  = ::chronon3d::RendererDirtyTelemetry;
    using RendererLayerHistory    = ::chronon3d::RendererLayerHistory;

    [[nodiscard]] RendererFrameHistory& frame_history() { return m_session.common.frame_history; }
    [[nodiscard]] const RendererFrameHistory& frame_history() const { return m_session.common.frame_history; }
    [[nodiscard]] RendererDirtyTelemetry& dirty_telemetry() { return m_session.common.dirty_telemetry; }
    [[nodiscard]] const RendererDirtyTelemetry& dirty_telemetry() const { return m_session.common.dirty_telemetry; }
    [[nodiscard]] RendererLayerHistory& layer_history() { return m_session.common.layer_history; }
    [[nodiscard]] const RendererLayerHistory& layer_history() const { return m_session.common.layer_history; }
    // WP-8 follow-up — scene_hasher + program_store ownership moved
    // off RenderSession onto RenderRuntime.  These convenience
    // accessors now route through m_runtime (the renderer always has
    // a valid runtime pointer per the ctor init list guarantees).
    [[nodiscard]] graph::SceneHasher& scene_hasher() { return m_runtime->scene_hasher(); }
    [[nodiscard]] const graph::SceneHasher& scene_hasher() const { return m_runtime->scene_hasher(); }
    [[nodiscard]] graph::SceneProgramStore& program_store() { return m_runtime->program_store(); }
    [[nodiscard]] const graph::SceneProgramStore& program_store() const { return m_runtime->program_store(); }

    // ── Convenience methods for graph pipeline orchestration ────────────
    void mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp) {
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

    void commit_frame_state(Frame frame, const Camera2_5D& cam,
                             uint64_t combined_fp, uint64_t static_fp,
                             uint64_t structure_fp, uint64_t active_at_fp,
                             std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
        commit_prev_frame_state(frame, cam, combined_fp, static_fp,
                                structure_fp, active_at_fp, std::move(layer_bboxes));
    }

    void commit_prev_frame_state(Frame frame, const Camera2_5D& cam,
                                  uint64_t combined_fp, uint64_t static_fp,
                                  uint64_t structure_fp, uint64_t active_at_fp,
                                  std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
        m_session.common.layer_history.prev_layer_bboxes = std::move(layer_bboxes);
        m_session.common.frame_history.prev_frame = frame;
        m_session.common.frame_history.prev_scene_fingerprint = combined_fp;
        m_session.common.frame_history.prev_static_scene_fingerprint = static_fp;
        m_session.common.frame_history.prev_graph_structure_fingerprint = structure_fp;
        m_session.common.frame_history.prev_active_at_fingerprint = active_at_fp;
        m_session.common.frame_history.prev_camera = cam;
        m_session.common.frame_history.prev_camera_valid = cam.enabled;
    }

    void update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                 bool tile_execution_used, bool fast_path_reused,
                                 bool graph_reused) {
        m_session.common.dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
        m_session.common.dirty_telemetry.last_dirty_rect = rect;
        m_session.common.dirty_telemetry.last_tile_execution_used = tile_execution_used;
        m_session.common.dirty_telemetry.last_fast_path_reused = fast_path_reused;
        m_session.common.dirty_telemetry.last_graph_reused = graph_reused;
    }

    // ── RAII buffer management ──────────────────────────────────────
    [[nodiscard]] RendererBufferRing& buffer_ring() { return m_session.software.buffer_ring; }
    [[nodiscard]] const RendererBufferRing& buffer_ring() const { return m_session.software.buffer_ring; }
    [[nodiscard]] TransformScratchBuffer& scratch_buffer() { return m_session.software.scratch_buffer; }
    [[nodiscard]] const TransformScratchBuffer& scratch_buffer() const { return m_session.software.scratch_buffer; }

private:
    Config            m_config;
    RenderSettings    m_settings{};
    RenderCounters    m_counters;
    ImageRenderer     m_image_renderer;

    std::shared_ptr<media::MediaFrameProvider> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;

    const CompositionRegistry* m_registry{nullptr};

    // TICKET-011 — m_runtime is a POINTER.  Set in ctor init list to
    // either an external reference (production path) or to
    // m_owned_runtime_storage.get() (transitional path).  Use
    // m_runtime->X() everywhere; never m_runtime.X().
    runtime::RenderRuntime* m_runtime{nullptr};
    std::unique_ptr<runtime::RenderRuntime> m_owned_runtime_storage;  // transitional ctor only

    SoftwareRenderSession    m_session;
};

} // namespace chronon3d
