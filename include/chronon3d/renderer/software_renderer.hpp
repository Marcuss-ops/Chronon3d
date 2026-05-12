#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/renderer/text_renderer.hpp>
#include <chronon3d/renderer/image_renderer.hpp>
#include <chronon3d/math/transform.hpp>

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

    // Diagnostic mode control
    void set_diagnostic_mode(bool enabled) { diagnostic_ = enabled; }
    [[nodiscard]] bool is_diagnostic_mode() const { return diagnostic_; }

private:
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                   const Camera& camera, i32 width, i32 height);

    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color);

    void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                               const Mat4& view, const Mat4& proj, const Color& color);

    // Effect renderers — drawn before the main shape
    void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state);
    void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state);

    // Diagnostic drawing helpers
    void draw_diagnostic_info(Framebuffer& fb, const RenderNode& node, const RenderState& state);

    TextRenderer m_text_renderer;
    ImageRenderer m_image_renderer;
    bool diagnostic_{false};
};

} // namespace chronon3d
