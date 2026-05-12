#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/renderer/text_renderer.hpp>

namespace chronon3d {

/**
 * SoftwareRenderer — CPU-based rasterizer.
 *
 * Coordinate system:
 *   Origin (0,0) = Top-Left. X right, Y down.
 *   Rect / RoundedRect / Circle: center-based positioning.
 *   Line: absolute pixel coordinates.
 *
 * Draw order: Painter's Algorithm (insertion order).
 * For each node: shadow → glow → shape.
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
    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color);

    void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                               const Mat4& view, const Mat4& proj, const Color& color);

    // Effect renderers — drawn before the main shape
    void draw_shadow(Framebuffer& fb, const RenderNode& node);
    void draw_glow(Framebuffer& fb, const RenderNode& node);

    // Diagnostic drawing helpers
    void draw_diagnostic_info(Framebuffer& fb, const RenderNode& node);

    TextRenderer m_text_renderer;
    bool diagnostic_{false};
};

} // namespace chronon3d
