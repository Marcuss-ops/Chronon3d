#pragma once

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/text/text_renderer.hpp>
#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/effects/layer_effect.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/runtime/graph_executor.hpp>
#include <chronon3d/backends/software/fake_extruded_text_renderer.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/backends/text/font_backend.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/scene/camera/camera.hpp>

#include <memory>
#include <optional>
#include <unordered_map>

namespace chronon3d {

class SoftwareRenderer;

namespace software_internal {
    std::unique_ptr<Framebuffer> render_frame(SoftwareRenderer& renderer, const Composition& comp,
                                             Frame frame);
    std::unique_ptr<Framebuffer> render_scene_internal(SoftwareRenderer& renderer,
                                                       const Scene& scene, const Camera& camera,
                                                       i32 width, i32 height, Frame frame,
                                                       f32 frame_time);
    std::string debug_render_graph(const SoftwareRenderer& renderer, const Scene& scene,
                                   const Camera& camera, i32 width, i32 height,
                                   Frame frame, f32 frame_time);
}

/**
 * SoftwareRenderer — CPU-based rasterizer.
 *
 * Support for hierarchical layers, inverse mapping, and transform-aware effects.
 */
class SoftwareRenderer : public Renderer {
public:
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame);
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
        m_text_renderer.clear_cache();
        m_node_cache.clear();
        // Video cache clearing is now responsibility of the decoder implementation
    }

    void set_composition_registry(const CompositionRegistry* registry) { m_registry = registry; }
    [[nodiscard]] const CompositionRegistry* composition_registry() const { return m_registry; }

    [[nodiscard]] TextRenderer& text_renderer() { return m_text_renderer; }
    [[nodiscard]] ImageRenderer& image_renderer() { return m_image_renderer; }
    [[nodiscard]] FakeExtrudedTextRenderer& fake_extruded_text_renderer() {
        return m_fake_extruded_text_renderer;
    }
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

    void set_font_backend(std::shared_ptr<text::FontBackend> backend);
    [[nodiscard]] text::FontBackend* font_backend() const {
        return m_font_backend.get();
    }

    // Public for use by graph nodes via RenderGraphContext.
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, i32 width, i32 height);
    static void apply_blur(Framebuffer& fb, f32 radius);
    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack);
    static void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode);

    [[nodiscard]] renderer::SoftwareRegistry& software_registry() { return *m_software_registry; }
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const { return *m_software_registry; }

    SoftwareRenderer();

private:
    std::unique_ptr<Framebuffer> render_scene_internal(const Scene& scene, const Camera& camera,
                                                       i32 width, i32 height, Frame frame,
                                                       f32 frame_time);

    TextRenderer      m_text_renderer;
    ImageRenderer     m_image_renderer;
    FakeExtrudedTextRenderer m_fake_extruded_text_renderer;
    mutable cache::NodeCache  m_node_cache;

    std::shared_ptr<video::VideoFrameDecoder> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;
    std::shared_ptr<text::FontBackend> m_font_backend;

    std::unique_ptr<renderer::SoftwareRegistry> m_software_registry;

    RenderSettings    m_settings{};
    const CompositionRegistry* m_registry{nullptr};
};

} // namespace chronon3d
