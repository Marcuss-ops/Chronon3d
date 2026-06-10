#pragma once

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/media/frame_source_provider.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/backends/software/graph_cache.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

namespace chronon3d::graph {
    class GraphExecutor;
}

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
private:
    std::shared_ptr<cache::FramebufferPool> m_framebuffer_pool;

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
        m_node_cache.clear();
        if (m_framebuffer_pool) m_framebuffer_pool->clear();
        m_graph_cache.reset();
        m_frame_history.prev_graph_structure_fingerprint = 0;
        m_buffer_ring.reset();
        m_scratch_buffer.reset();
        // Video cache clearing is now responsibility of the decoder implementation
    }

    void set_composition_registry(const CompositionRegistry* registry) { m_registry = registry; }
    [[nodiscard]] const CompositionRegistry* composition_registry() const { return m_registry; }

    [[nodiscard]] ImageRenderer& image_renderer() { return m_image_renderer; }
    [[nodiscard]] cache::NodeCache& node_cache() { return m_node_cache; }
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
    [[nodiscard]] double last_dirty_area_ratio() const { return m_dirty_telemetry.last_dirty_area_ratio; }
    [[nodiscard]] bool last_dirty_rect_enabled() const { return m_dirty_telemetry.last_dirty_rect_enabled; }
    [[nodiscard]] std::optional<raster::BBox> last_dirty_rect() const { return m_dirty_telemetry.last_dirty_rect; }
    [[nodiscard]] bool last_tile_execution_used() const { return m_dirty_telemetry.last_tile_execution_used; }
    [[nodiscard]] bool last_fast_path_reused() const { return m_dirty_telemetry.last_fast_path_reused; }
    [[nodiscard]] bool last_graph_reused() const { return m_dirty_telemetry.last_graph_reused; }
    [[nodiscard]] int last_layer_count() const { return m_dirty_telemetry.last_layer_count; }

    // Public for use by graph nodes via RenderGraphContext.
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, i32 width, i32 height) override;
    void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt) override;
    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip = std::nullopt) override;
    void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip = std::nullopt) override;

    [[nodiscard]] renderer::SoftwareRegistry& software_registry() { return *m_software_registry; }
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const { return *m_software_registry; }

    std::shared_ptr<cache::FramebufferPool> framebuffer_pool() override { return m_framebuffer_pool; }
    [[nodiscard]] cache::FramebufferPool& software_framebuffer_pool() { return *m_framebuffer_pool; }
    [[nodiscard]] const cache::FramebufferPool& software_framebuffer_pool() const { return *m_framebuffer_pool; }
    [[nodiscard]] RenderCounters* counters() override { return &m_counters; }
    [[nodiscard]] const RenderCounters* counters() const { return &m_counters; }
    [[nodiscard]] graph::GraphExecutor* executor();
    [[nodiscard]] const graph::GraphExecutor* executor() const;

    SoftwareRenderer();
    ~SoftwareRenderer() override;

    // Non-copyable.  Movable: the RAII member types (RendererBufferRing,
    // TransformScratchBuffer) all support move semantics, and the rest
    // of the members (shared_ptr, unique_ptr, plain data) are
    // trivially movable.  Callers may move SoftwareRenderer as long as
    // no outstanding Handle or dangling pointer references the old location.
    SoftwareRenderer(const SoftwareRenderer&) = delete;
    SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
    SoftwareRenderer(SoftwareRenderer&&) noexcept = default;
    SoftwareRenderer& operator=(SoftwareRenderer&&) noexcept = default;

    struct LayerBBoxState {
        raster::BBox bbox;
        Mat4 world_matrix;
        f32 opacity{1.0f};
        bool visible{true};
        bool cache_static{false};
        bool uses_2_5d_projection{false};
        uint64_t content_hash{0};
    };

    // ── Encapsulated per-frame state structs ──────────────────────────────
    struct RendererFrameHistory {
        Frame prev_frame{-1};
        Camera2_5D prev_camera;
        bool prev_camera_valid{false};
        uint64_t prev_scene_fingerprint{0};
        uint64_t prev_static_scene_fingerprint{0};
        uint64_t prev_graph_structure_fingerprint{0};
        uint64_t prev_active_at_fingerprint{0};
    };

    struct RendererDirtyTelemetry {
        double last_dirty_area_ratio{1.0};
        int last_layer_count{0};
        bool last_dirty_rect_enabled{false};
        std::optional<raster::BBox> last_dirty_rect;
        bool last_tile_execution_used{false};
        bool last_fast_path_reused{false};
        bool last_graph_reused{false};
    };

    struct RendererLayerHistory {
        std::unordered_map<std::string, LayerBBoxState> prev_layer_bboxes;
    };

    // ── Accessor methods for encapsulated state ──────────────────────────
    [[nodiscard]] RendererFrameHistory& frame_history() { return m_frame_history; }
    [[nodiscard]] const RendererFrameHistory& frame_history() const { return m_frame_history; }
    [[nodiscard]] RendererDirtyTelemetry& dirty_telemetry() { return m_dirty_telemetry; }
    [[nodiscard]] const RendererDirtyTelemetry& dirty_telemetry() const { return m_dirty_telemetry; }
    [[nodiscard]] RendererLayerHistory& layer_history() { return m_layer_history; }
    [[nodiscard]] const RendererLayerHistory& layer_history() const { return m_layer_history; }
    [[nodiscard]] graph::SceneHasher& scene_hasher() { return m_scene_hasher; }
    [[nodiscard]] const graph::SceneHasher& scene_hasher() const { return m_scene_hasher; }

    // ── Convenience methods for graph pipeline orchestration ────────────
    void mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp) {
        m_dirty_telemetry.last_dirty_area_ratio = 0.0;
        m_dirty_telemetry.last_dirty_rect_enabled = false;
        m_dirty_telemetry.last_dirty_rect = std::nullopt;
        m_dirty_telemetry.last_tile_execution_used = false;
        m_dirty_telemetry.last_fast_path_reused = true;
        m_dirty_telemetry.last_graph_reused = false;
        m_frame_history.prev_frame = frame;
        m_frame_history.prev_scene_fingerprint = combined_fp;
        m_frame_history.prev_camera = cam;
        m_frame_history.prev_camera_valid = cam.enabled;
    }

    void commit_prev_frame_state(Frame frame, const Camera2_5D& cam,
                                  uint64_t combined_fp, uint64_t static_fp,
                                  uint64_t structure_fp, uint64_t active_at_fp,
                                  std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
        m_layer_history.prev_layer_bboxes = std::move(layer_bboxes);
        m_frame_history.prev_frame = frame;
        m_frame_history.prev_scene_fingerprint = combined_fp;
        m_frame_history.prev_static_scene_fingerprint = static_fp;
        m_frame_history.prev_graph_structure_fingerprint = structure_fp;
        m_frame_history.prev_active_at_fingerprint = active_at_fp;
        m_frame_history.prev_camera = cam;
        m_frame_history.prev_camera_valid = cam.enabled;
    }

    void update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                 bool tile_execution_used, bool fast_path_reused,
                                 bool graph_reused) {
        m_dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
        m_dirty_telemetry.last_dirty_rect = rect;
        m_dirty_telemetry.last_tile_execution_used = tile_execution_used;
        m_dirty_telemetry.last_fast_path_reused = fast_path_reused;
        m_dirty_telemetry.last_graph_reused = graph_reused;
    }

    // ── RAII buffer management ──────────────────────────────────────────
    [[nodiscard]] RendererBufferRing& buffer_ring() { return m_buffer_ring; }
    [[nodiscard]] const RendererBufferRing& buffer_ring() const { return m_buffer_ring; }
    [[nodiscard]] TransformScratchBuffer& scratch_buffer() { return m_scratch_buffer; }
    [[nodiscard]] const TransformScratchBuffer& scratch_buffer() const { return m_scratch_buffer; }
    [[nodiscard]] CompiledGraphCache& graph_cache() { return m_graph_cache; }
    [[nodiscard]] const CompiledGraphCache& graph_cache() const { return m_graph_cache; }

private:
    ImageRenderer     m_image_renderer;
    mutable cache::NodeCache  m_node_cache;

    std::shared_ptr<media::MediaFrameProvider> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;

    std::unique_ptr<renderer::SoftwareRegistry> m_software_registry;

    RenderSettings    m_settings{};
    const CompositionRegistry* m_registry{nullptr};

    RenderCounters    m_counters;
    std::unique_ptr<graph::GraphExecutor> m_executor;

    RendererBufferRing    m_buffer_ring;
    TransformScratchBuffer m_scratch_buffer;
    CompiledGraphCache    m_graph_cache;

    // ── Encapsulated per-frame state ─────────────────────────────────────
    RendererFrameHistory     m_frame_history;
    RendererDirtyTelemetry   m_dirty_telemetry;
    RendererLayerHistory     m_layer_history;
    graph::SceneHasher       m_scene_hasher;
};

} // namespace chronon3d
