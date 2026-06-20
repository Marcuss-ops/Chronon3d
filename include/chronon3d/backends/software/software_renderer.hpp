#pragma once

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
#include <chronon3d/backends/software/renderer_cache_state.hpp>
#include <chronon3d/backends/software/renderer_runtime_resources.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/core/config.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <chronon3d/core/memory/render_session.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

namespace chronon3d {

namespace renderer {
void clear_text_glow_cache();
void clear_text_shadow_cache();
}

class SoftwareRenderer;
class CompositionRegistry;

/**
 * SoftwareRenderer — CPU-based rasterizer.
 *
 * Support for hierarchical layers, inverse mapping, and transform-aware effects.
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

    // Render settings management
    void set_settings(const RenderSettings& settings) {
        m_settings = settings;
        m_counters.program_cache_capacity.store(settings.program_cache_capacity, std::memory_order_relaxed);
        m_counters.program_cache_tune.store(settings.program_cache_tune ? 1 : 0, std::memory_order_relaxed);
    }
    [[nodiscard]] const RenderSettings& settings() const { return m_settings; }

    // Legacy/Compatibility setters (redirect to m_settings)
    void set_motion_blur(MotionBlurSettings mb) { m_settings.motion_blur = mb; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const { return m_settings.motion_blur; }

    void set_diagnostic_mode(bool enabled) { m_settings.diagnostics.enabled = enabled; }
    [[nodiscard]] bool is_diagnostic_mode() const { return m_settings.diagnostics.enabled; }

    // ── Intentional cache operations ──────────────────────────────────

    /// Clear all caches (image, font, node, pool, graph, frame state).
    /// Useful between unrelated render sessions.
    void clear_caches() {
        m_image_renderer.clear_cache();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
        renderer::clear_text_glow_cache();
        renderer::clear_text_shadow_cache();
#endif
        clear_node_cache();
        if (m_cache_state.framebuffer_pool) m_cache_state.framebuffer_pool->clear();
        m_cache_state.graph_cache.reset();
        m_session.frame_history.prev_graph_structure_fingerprint = 0;
        m_session.clear_per_frame();
        // Video cache clearing is now responsibility of the decoder implementation
    }

    /// Clear only the node cache (e.g. between unrelated render sessions
    /// where the composition/scene has changed).
    void clear_node_cache() { m_cache_state.node_cache.clear(); }

    /// Reset all profiling counters to their default (zero) state.
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
    [[nodiscard]] cache::NodeCache& node_cache() { return m_cache_state.node_cache; }
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
    [[nodiscard]] double last_dirty_area_ratio() const { return m_session.dirty_telemetry.last_dirty_area_ratio; }
    [[nodiscard]] bool last_dirty_rect_enabled() const { return m_session.dirty_telemetry.last_dirty_rect_enabled; }
    [[nodiscard]] std::optional<raster::BBox> last_dirty_rect() const { return m_session.dirty_telemetry.last_dirty_rect; }
    [[nodiscard]] bool last_tile_execution_used() const { return m_session.dirty_telemetry.last_tile_execution_used; }
    [[nodiscard]] bool last_fast_path_reused() const { return m_session.dirty_telemetry.last_fast_path_reused; }
    [[nodiscard]] bool last_graph_reused() const { return m_session.dirty_telemetry.last_graph_reused; }
    [[nodiscard]] int last_layer_count() const { return m_session.dirty_telemetry.last_layer_count; }

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

    // PR2 — declare text rendering support; downstream callers MUST gate on
    // `backend->capabilities().text_run` before invoking draw_text_run.
    // Also PR2 — `bool diagnostic_mode` was removed from this override:
    // diagnostic logging is now driven by the caller (e.g.
    // `ctx.options.diagnostics_enabled`) at the graph-node level, not as
    // a flag buried inside the processor's params.
    graph::RenderOpResult draw_text_run(Framebuffer& fb, const TextRunShape& shape, const Mat4& model_matrix,
                                        float opacity) override;

    [[nodiscard]] renderer::SoftwareRegistry& software_registry() { return *m_runtime_resources.software_registry; }
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const { return *m_runtime_resources.software_registry; }

    [[nodiscard]] graph::GraphNodeCatalog& graph_node_registry() { return *m_runtime_resources.graph_node_registry; }
    [[nodiscard]] const graph::GraphNodeCatalog& graph_node_registry() const { return *m_runtime_resources.graph_node_registry; }

    [[nodiscard]] effects::EffectCatalog& effect_catalog() { return *m_runtime_resources.effect_catalog; }
    [[nodiscard]] const effects::EffectCatalog& effect_catalog() const { return *m_runtime_resources.effect_catalog; }

    std::shared_ptr<cache::FramebufferPool> framebuffer_pool() override { return m_cache_state.framebuffer_pool; }
    [[nodiscard]] cache::FramebufferPool& software_framebuffer_pool() { return *m_cache_state.framebuffer_pool; }
    [[nodiscard]] const cache::FramebufferPool& software_framebuffer_pool() const { return *m_cache_state.framebuffer_pool; }
    [[nodiscard]] RenderCounters* counters() override { return &m_counters; }
    [[nodiscard]] const RenderCounters* counters() const { return &m_counters; }
    [[nodiscard]] graph::GraphExecutor* executor();
    [[nodiscard]] const graph::GraphExecutor* executor() const;

    /// Per-instance engine configuration (replaces legacy Config singleton)
    [[nodiscard]] const Config& config() const { return m_config; }
    [[nodiscard]] Config& config() { return m_config; }

    /// Default constructor — builds Config from environment variables.
    SoftwareRenderer();

    /// Constructor with an explicit Config instance (e.g. CLI budget override).
    explicit SoftwareRenderer(Config config);

    ~SoftwareRenderer() override;

    // Non-copyable.  Movable: all encapsulated state structs
    // (RenderSession, RendererCacheState, RendererRuntimeResources)
    // contain only moveable types (shared_ptr, unique_ptr, plain data).
    // RenderSession stores FrameArena behind unique_ptr for movability.
    // Callers may move SoftwareRenderer as long as no outstanding Handle
    // or dangling pointer references the old location.
    SoftwareRenderer(const SoftwareRenderer&) = delete;
    SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
    SoftwareRenderer(SoftwareRenderer&&) noexcept = default;
    SoftwareRenderer& operator=(SoftwareRenderer&&) noexcept = default;

    // ── RenderSession access ────────────────────────────────────────────
    [[nodiscard]] RenderSession& session() { return m_session; }
    [[nodiscard]] const RenderSession& session() const { return m_session; }

    // ── Convenience accessors (forward to session) ──────────────────────
    using LayerBBoxState          = ::chronon3d::LayerBBoxState;
    using RendererFrameHistory    = ::chronon3d::RendererFrameHistory;
    using RendererDirtyTelemetry  = ::chronon3d::RendererDirtyTelemetry;
    using RendererLayerHistory    = ::chronon3d::RendererLayerHistory;

    [[nodiscard]] RendererFrameHistory& frame_history() { return m_session.frame_history; }
    [[nodiscard]] const RendererFrameHistory& frame_history() const { return m_session.frame_history; }
    [[nodiscard]] RendererDirtyTelemetry& dirty_telemetry() { return m_session.dirty_telemetry; }
    [[nodiscard]] const RendererDirtyTelemetry& dirty_telemetry() const { return m_session.dirty_telemetry; }
    [[nodiscard]] RendererLayerHistory& layer_history() { return m_session.layer_history; }
    [[nodiscard]] const RendererLayerHistory& layer_history() const { return m_session.layer_history; }
    [[nodiscard]] graph::SceneHasher& scene_hasher() { return m_session.scene_hasher; }
    [[nodiscard]] const graph::SceneHasher& scene_hasher() const { return m_session.scene_hasher; }

    // ── Convenience methods for graph pipeline orchestration ────────────
    void mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp) {
        m_session.dirty_telemetry.last_dirty_area_ratio = 0.0;
        m_session.dirty_telemetry.last_dirty_rect_enabled = false;
        m_session.dirty_telemetry.last_dirty_rect = std::nullopt;
        m_session.dirty_telemetry.last_tile_execution_used = false;
        m_session.dirty_telemetry.last_fast_path_reused = true;
        m_session.dirty_telemetry.last_graph_reused = false;
        m_session.frame_history.prev_frame = frame;
        m_session.frame_history.prev_scene_fingerprint = combined_fp;
        m_session.frame_history.prev_camera = cam;
        m_session.frame_history.prev_camera_valid = cam.enabled;
    }

    /// Alias for backward compatibility — prefer commit_prev_frame_state().
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
        m_session.layer_history.prev_layer_bboxes = std::move(layer_bboxes);
        m_session.frame_history.prev_frame = frame;
        m_session.frame_history.prev_scene_fingerprint = combined_fp;
        m_session.frame_history.prev_static_scene_fingerprint = static_fp;
        m_session.frame_history.prev_graph_structure_fingerprint = structure_fp;
        m_session.frame_history.prev_active_at_fingerprint = active_at_fp;
        m_session.frame_history.prev_camera = cam;
        m_session.frame_history.prev_camera_valid = cam.enabled;
    }

    void update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                 bool tile_execution_used, bool fast_path_reused,
                                 bool graph_reused) {
        m_session.dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
        m_session.dirty_telemetry.last_dirty_rect = rect;
        m_session.dirty_telemetry.last_tile_execution_used = tile_execution_used;
        m_session.dirty_telemetry.last_fast_path_reused = fast_path_reused;
        m_session.dirty_telemetry.last_graph_reused = graph_reused;
    }

    // ── RAII buffer management ──────────────────────────────────────────
    [[nodiscard]] RendererBufferRing& buffer_ring() { return m_session.buffer_ring; }
    [[nodiscard]] const RendererBufferRing& buffer_ring() const { return m_session.buffer_ring; }
    [[nodiscard]] TransformScratchBuffer& scratch_buffer() { return m_session.scratch_buffer; }
    [[nodiscard]] const TransformScratchBuffer& scratch_buffer() const { return m_session.scratch_buffer; }
    [[nodiscard]] graph::CompiledGraphCache& graph_cache() { return m_cache_state.graph_cache; }
    [[nodiscard]] const graph::CompiledGraphCache& graph_cache() const { return m_cache_state.graph_cache; }

    /// Access the underlying RenderBackend implementation (extracted in PR-9).
    [[nodiscard]] graph::RenderBackend& backend() { return *m_backend; }
    [[nodiscard]] const graph::RenderBackend& backend() const { return *m_backend; }

private:
    ImageRenderer     m_image_renderer;

    std::shared_ptr<media::MediaFrameProvider> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;

    RenderSettings    m_settings{};
    const CompositionRegistry* m_registry{nullptr};

    RenderCounters    m_counters;

    // ── Per-instance engine configuration ───────────────────────────
    Config            m_config;

    // ── Encapsulated cache state ─────────────────────────────────────
    RendererCacheState       m_cache_state;
    // ── Encapsulated runtime resources ─────────────────────────────────
    RendererRuntimeResources m_runtime_resources;
    // ── Encapsulated per-session state ───────────────────────────────────
    RenderSession            m_session;

    // ── Extracted RenderBackend implementation (PR-9) ───────────────────
    // Pure rendering operations (blur, composite, effects, DoF) live here.
    // draw_node and draw_text_run remain on SoftwareRenderer pending
    // ShapeProcessor migration to accept RenderBackend&.
    std::unique_ptr<SoftwareBackend> m_backend;
};

} // namespace chronon3d
