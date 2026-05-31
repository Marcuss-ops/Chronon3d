#pragma once

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/effects/layer_effect.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/scene_hasher.hpp>

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
    void set_settings(const RenderSettings& settings) { m_settings = settings; }
    [[nodiscard]] const RenderSettings& settings() const { return m_settings; }

    // Legacy/Compatibility setters (redirect to m_settings)
    void set_motion_blur(MotionBlurSettings mb) { m_settings.motion_blur = mb; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const { return m_settings.motion_blur; }

    void set_diagnostic_mode(bool enabled) { m_settings.diagnostic = enabled; }
    [[nodiscard]] bool is_diagnostic_mode() const { return m_settings.diagnostic; }

    // Clear image and font caches (useful between unrelated render sessions)
    void clear_caches() {
        m_image_renderer.clear_cache();
        renderer::clear_text_glow_cache();
        renderer::clear_text_shadow_cache();
        m_node_cache.clear();
        if (m_framebuffer_pool) m_framebuffer_pool->clear();
        m_cached_compiled_graph.reset();
        m_cached_compiled_width = 0;
        m_cached_compiled_height = 0;
        m_cached_compiled_structure_hash = 0;
        m_prev_graph_structure_fingerprint = 0;
        // Video cache clearing is now responsibility of the decoder implementation
    }

    void set_composition_registry(const CompositionRegistry* registry) { m_registry = registry; }
    [[nodiscard]] const CompositionRegistry* composition_registry() const { return m_registry; }

    [[nodiscard]] ImageRenderer& image_renderer() { return m_image_renderer; }
    [[nodiscard]] cache::NodeCache& node_cache() { return m_node_cache; }
    [[nodiscard]] const RenderSettings& render_settings() const { return m_settings; }

    void set_video_decoder(std::shared_ptr<video::VideoFrameDecoder> decoder) {
        m_video_decoder = std::move(decoder);
    }
    [[nodiscard]] video::VideoFrameDecoder* video_decoder() const {
        return m_video_decoder.get();
    }

    void set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
        m_image_backend = std::move(backend);
        m_image_renderer.set_backend(m_image_backend);
    }
    [[nodiscard]] image::ImageBackend* image_backend() const {
        return m_image_backend.get();
    }

    [[nodiscard]] double last_dirty_area_ratio() const { return m_last_dirty_area_ratio; }

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

    // Dirty rectangles tracking
    std::shared_ptr<Framebuffer> m_prev_framebuffer;
    std::unordered_map<std::string, LayerBBoxState> m_prev_layer_bboxes;
    Frame m_prev_frame{-1};
    double m_last_dirty_area_ratio{1.0};
    int m_last_layer_count{0};
    Camera2_5D m_prev_camera;
    bool m_prev_camera_valid{false};
    uint64_t m_prev_scene_fingerprint{0};
    uint64_t m_prev_static_scene_fingerprint{0};
    uint64_t m_prev_graph_structure_fingerprint{0};
    uint64_t m_prev_active_at_fingerprint{0}; // tracks which layers are active at each frame
    graph::SceneHasher m_scene_hasher;

    /// Cached compiled render graph for incremental rebuild (Phase 1).
    /// When graph_structure_unchanged is true, the previously built,
    /// optimized and compiled graph is reused — skipping build_graph(), optimize_graph() and compile().
    std::unique_ptr<graph::CompiledFrameGraph> m_cached_compiled_graph;
    int m_cached_compiled_width{0};
    int m_cached_compiled_height{0};
    uint64_t m_cached_compiled_structure_hash{0};

    [[nodiscard]] int last_layer_count() const { return m_last_layer_count; }

private:
    ImageRenderer     m_image_renderer;
    mutable cache::NodeCache  m_node_cache;

    std::shared_ptr<video::VideoFrameDecoder> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;

    std::unique_ptr<renderer::SoftwareRegistry> m_software_registry;

    RenderSettings    m_settings{};
    const CompositionRegistry* m_registry{nullptr};

    RenderCounters    m_counters;
    std::unique_ptr<graph::GraphExecutor> m_executor;
};

} // namespace chronon3d
