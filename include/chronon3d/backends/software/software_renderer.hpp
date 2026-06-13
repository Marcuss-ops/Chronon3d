#pragma once

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/backends/software/renderer_types.hpp>
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
#include <chronon3d/backends/software/renderer_frame_state.hpp>
#include <chronon3d/backends/software/renderer_cache_state.hpp>
#include <chronon3d/backends/software/renderer_runtime_resources.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
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
        simd::g_force_scalar_normal_blend.store(settings.force_scalar_normal_blend, std::memory_order_relaxed);
        m_counters.program_cache_capacity.store(settings.program_cache_capacity, std::memory_order_relaxed);
        m_counters.program_cache_tune.store(settings.program_cache_tune ? 1 : 0, std::memory_order_relaxed);
    }
    [[nodiscard]] const RenderSettings& settings() const { return m_settings; }

    // Legacy/Compatibility setters (redirect to m_settings)
    void set_motion_blur(MotionBlurSettings mb) { m_settings.motion_blur = mb; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const { return m_settings.motion_blur; }

    void set_diagnostic_mode(bool enabled) { m_settings.diagnostics.enabled = enabled; }
    [[nodiscard]] bool is_diagnostic_mode() const { return m_settings.diagnostics.enabled; }

    // Clear image and font caches (useful between unrelated render sessions)
    void clear_caches() {
        m_image_renderer.clear_cache();
        renderer::clear_text_glow_cache();
        renderer::clear_text_shadow_cache();
        m_cache_state.node_cache.clear();
        if (m_cache_state.framebuffer_pool) m_cache_state.framebuffer_pool->clear();
        m_cache_state.graph_cache.reset();
        m_frame_state.frame_history.prev_graph_structure_fingerprint = 0;
        m_frame_state.buffer_ring.reset();
        m_frame_state.scratch_buffer.reset();
        // Video cache clearing is now responsibility of the decoder implementation
    }

    void set_composition_registry(const CompositionRegistry* registry) { m_registry = registry; }
    [[nodiscard]] const CompositionRegistry* composition_registry() const { return m_registry; }

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
    [[nodiscard]] double last_dirty_area_ratio() const { return m_frame_state.dirty_telemetry.last_dirty_area_ratio; }
    [[nodiscard]] bool last_dirty_rect_enabled() const { return m_frame_state.dirty_telemetry.last_dirty_rect_enabled; }
    [[nodiscard]] std::optional<raster::BBox> last_dirty_rect() const { return m_frame_state.dirty_telemetry.last_dirty_rect; }
    [[nodiscard]] bool last_tile_execution_used() const { return m_frame_state.dirty_telemetry.last_tile_execution_used; }
    [[nodiscard]] bool last_fast_path_reused() const { return m_frame_state.dirty_telemetry.last_fast_path_reused; }
    [[nodiscard]] bool last_graph_reused() const { return m_frame_state.dirty_telemetry.last_graph_reused; }
    [[nodiscard]] int last_layer_count() const { return m_frame_state.dirty_telemetry.last_layer_count; }

    // Public for use by graph nodes via RenderGraphContext.
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, i32 width, i32 height) override;
    void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt) override;
    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip = std::nullopt) override;
    void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip = std::nullopt, CompositeOperator op = CompositeOperator::SourceOver) override;

    [[nodiscard]] renderer::SoftwareRegistry& software_registry() { return *m_runtime_resources.software_registry; }
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const { return *m_runtime_resources.software_registry; }

    std::shared_ptr<cache::FramebufferPool> framebuffer_pool() override { return m_cache_state.framebuffer_pool; }
    [[nodiscard]] cache::FramebufferPool& software_framebuffer_pool() { return *m_cache_state.framebuffer_pool; }
    [[nodiscard]] const cache::FramebufferPool& software_framebuffer_pool() const { return *m_cache_state.framebuffer_pool; }
    [[nodiscard]] RenderCounters* counters() override { return &m_counters; }
    [[nodiscard]] const RenderCounters* counters() const { return &m_counters; }
    [[nodiscard]] graph::GraphExecutor* executor();
    [[nodiscard]] const graph::GraphExecutor* executor() const;

    SoftwareRenderer();
    ~SoftwareRenderer() override;

    // Non-copyable.  Movable: all encapsulated state structs
    // (RendererFrameState, RendererCacheState, RendererRuntimeResources)
    // contain only moveable types (shared_ptr, unique_ptr, plain data).
    // Callers may move SoftwareRenderer as long as no outstanding Handle
    // or dangling pointer references the old location.
    SoftwareRenderer(const SoftwareRenderer&) = delete;
    SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
    SoftwareRenderer(SoftwareRenderer&&) noexcept = default;
    SoftwareRenderer& operator=(SoftwareRenderer&&) noexcept = default;

    // ── Accessor methods for per-frame state ────────────────────────────
    using LayerBBoxState          = chronon3d::LayerBBoxState;
    using RendererFrameHistory    = chronon3d::RendererFrameHistory;
    using RendererDirtyTelemetry  = chronon3d::RendererDirtyTelemetry;
    using RendererLayerHistory    = chronon3d::RendererLayerHistory;

    [[nodiscard]] RendererFrameHistory& frame_history() { return m_frame_state.frame_history; }
    [[nodiscard]] const RendererFrameHistory& frame_history() const { return m_frame_state.frame_history; }
    [[nodiscard]] RendererDirtyTelemetry& dirty_telemetry() { return m_frame_state.dirty_telemetry; }
    [[nodiscard]] const RendererDirtyTelemetry& dirty_telemetry() const { return m_frame_state.dirty_telemetry; }
    [[nodiscard]] RendererLayerHistory& layer_history() { return m_frame_state.layer_history; }
    [[nodiscard]] const RendererLayerHistory& layer_history() const { return m_frame_state.layer_history; }
    [[nodiscard]] graph::SceneHasher& scene_hasher() { return m_frame_state.scene_hasher; }
    [[nodiscard]] const graph::SceneHasher& scene_hasher() const { return m_frame_state.scene_hasher; }

    // ── Convenience methods for graph pipeline orchestration ────────────
    void mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp) {
        m_frame_state.dirty_telemetry.last_dirty_area_ratio = 0.0;
        m_frame_state.dirty_telemetry.last_dirty_rect_enabled = false;
        m_frame_state.dirty_telemetry.last_dirty_rect = std::nullopt;
        m_frame_state.dirty_telemetry.last_tile_execution_used = false;
        m_frame_state.dirty_telemetry.last_fast_path_reused = true;
        m_frame_state.dirty_telemetry.last_graph_reused = false;
        m_frame_state.frame_history.prev_frame = frame;
        m_frame_state.frame_history.prev_scene_fingerprint = combined_fp;
        m_frame_state.frame_history.prev_camera = cam;
        m_frame_state.frame_history.prev_camera_valid = cam.enabled;
    }

    void commit_prev_frame_state(Frame frame, const Camera2_5D& cam,
                                  uint64_t combined_fp, uint64_t static_fp,
                                  uint64_t structure_fp, uint64_t active_at_fp,
                                  std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
        m_frame_state.layer_history.prev_layer_bboxes = std::move(layer_bboxes);
        m_frame_state.frame_history.prev_frame = frame;
        m_frame_state.frame_history.prev_scene_fingerprint = combined_fp;
        m_frame_state.frame_history.prev_static_scene_fingerprint = static_fp;
        m_frame_state.frame_history.prev_graph_structure_fingerprint = structure_fp;
        m_frame_state.frame_history.prev_active_at_fingerprint = active_at_fp;
        m_frame_state.frame_history.prev_camera = cam;
        m_frame_state.frame_history.prev_camera_valid = cam.enabled;
    }

    void update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                 bool tile_execution_used, bool fast_path_reused,
                                 bool graph_reused) {
        m_frame_state.dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
        m_frame_state.dirty_telemetry.last_dirty_rect = rect;
        m_frame_state.dirty_telemetry.last_tile_execution_used = tile_execution_used;
        m_frame_state.dirty_telemetry.last_fast_path_reused = fast_path_reused;
        m_frame_state.dirty_telemetry.last_graph_reused = graph_reused;
    }

    // ── RAII buffer management ──────────────────────────────────────────
    [[nodiscard]] RendererBufferRing& buffer_ring() { return m_frame_state.buffer_ring; }
    [[nodiscard]] const RendererBufferRing& buffer_ring() const { return m_frame_state.buffer_ring; }
    [[nodiscard]] TransformScratchBuffer& scratch_buffer() { return m_frame_state.scratch_buffer; }
    [[nodiscard]] const TransformScratchBuffer& scratch_buffer() const { return m_frame_state.scratch_buffer; }
    [[nodiscard]] CompiledGraphCache& graph_cache() { return m_cache_state.graph_cache; }
    [[nodiscard]] const CompiledGraphCache& graph_cache() const { return m_cache_state.graph_cache; }

private:
    ImageRenderer     m_image_renderer;

    std::shared_ptr<media::MediaFrameProvider> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;

    RenderSettings    m_settings{};
    const CompositionRegistry* m_registry{nullptr};

    RenderCounters    m_counters;

    // ── Encapsulated cache state ─────────────────────────────────────
    RendererCacheState       m_cache_state;
    // ── Encapsulated runtime resources ─────────────────────────────────
    RendererRuntimeResources m_runtime_resources;
    // ── Encapsulated per-frame state ─────────────────────────────────────
    RendererFrameState       m_frame_state;
};

} // namespace chronon3d
