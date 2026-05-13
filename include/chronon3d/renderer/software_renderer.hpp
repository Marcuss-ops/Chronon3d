#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/renderer/render_graph.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/renderer/text_renderer.hpp>
#include <chronon3d/renderer/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/layer_effect.hpp>
#include <chronon3d/scene/effect_stack.hpp>
#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <unordered_map>

namespace chronon3d {

/**
 * SoftwareRenderer — CPU-based rasterizer.
 *
 * Support for hierarchical layers, inverse mapping, and transform-aware effects.
 */
class SoftwareRenderer : public Renderer {
public:
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) override;
    std::unique_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera,
                                               i32 width, i32 height) override;
    [[nodiscard]] std::string debug_render_graph(const Scene& scene, const Camera& camera,
                                                 i32 width, i32 height, Frame frame = 0,
                                                 f32 frame_time = 0.0f) const;

    // Motion blur: accumulate N subframes when enabled.
    void set_motion_blur(MotionBlurSettings mb) { m_motion_blur = mb; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const { return m_motion_blur; }

    // Diagnostic mode control
    void set_diagnostic_mode(bool enabled) { diagnostic_ = enabled; }
    [[nodiscard]] bool is_diagnostic_mode() const { return diagnostic_; }

    // Clear image and font caches (useful between unrelated render sessions)
    void clear_caches() {
        m_image_renderer.clear_cache();
        m_text_renderer.clear_cache();
        m_node_cache.clear();
    }


    friend class rendergraph::RenderGraph;

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
    rendergraph::RenderGraph build_render_graph(const Scene& scene, const Camera& camera,
                                                i32 width, i32 height, Frame frame,
                                                f32 frame_time) const;

    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color);

    void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                               const Mat4& view, const Mat4& proj, const Color& color);

    void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state);
    void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state);

    void render_layer_nodes(Framebuffer& fb, const Layer& layer,
                            const RenderState& layer_state,
                            const Camera& camera, i32 width, i32 height);

    static void apply_color_effects(Framebuffer& fb, const LayerEffect& effect);

    // Diagnostic drawing helpers
    void draw_diagnostic_info(Framebuffer& fb, const RenderNode& node, const RenderState& state);

    TextRenderer      m_text_renderer;
    ImageRenderer     m_image_renderer;
    mutable cache::NodeCache  m_node_cache;
    bool              diagnostic_{false};
    MotionBlurSettings m_motion_blur{};
};


} // namespace chronon3d
