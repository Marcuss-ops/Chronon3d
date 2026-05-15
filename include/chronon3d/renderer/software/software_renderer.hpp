#pragma once

#include <chronon3d/renderer/software/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/renderer/text/text_renderer.hpp>
#include <chronon3d/renderer/assets/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/effects/layer_effect.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/renderer/software/render_settings.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/runtime/graph_executor.hpp>
#include <chronon3d/renderer/software/fake_extruded_text_renderer.hpp>
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
    void draw_node(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                   const RenderState& state, const Camera& camera, i32 width, i32 height);
}

/**
 * SoftwareRenderer — CPU-based rasterizer.
 *
 * Support for hierarchical layers, inverse mapping, and transform-aware effects.
 */
class SoftwareRenderer : public Renderer {
public:
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) override;
    std::unique_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera,
                                               i32 width, i32 height, Frame frame = 0) override;
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
#ifdef CHRONON_WITH_VIDEO
        m_video_extractor.clear_memory_cache();
#endif
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

    // Public for use by graph nodes via RenderGraphContext.
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, i32 width, i32 height);
    static void apply_blur(Framebuffer& fb, f32 radius);
    static void apply_effect_stack(Framebuffer& fb, const EffectStack& stack);
    static void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode);

private:
    std::unique_ptr<Framebuffer> render_scene_internal(const Scene& scene, const Camera& camera,
                                                       i32 width, i32 height, Frame frame,
                                                       f32 frame_time);
    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color);

    TextRenderer      m_text_renderer;
    ImageRenderer     m_image_renderer;
    FakeExtrudedTextRenderer m_fake_extruded_text_renderer;
    mutable cache::NodeCache  m_node_cache;
#ifdef CHRONON_WITH_VIDEO
    video::FfmpegFrameExtractor m_video_extractor;
#endif
    RenderSettings    m_settings{};
    const CompositionRegistry* m_registry{nullptr};
};


} // namespace chronon3d
