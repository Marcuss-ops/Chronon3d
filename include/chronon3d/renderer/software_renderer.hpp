#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

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

private:
    // Primitive rasterizers
    void draw_rect(Framebuffer& fb, const Vec3& position, const Vec2& size,
                   const Color& color, BlendMode mode);

    void draw_rounded_rect(Framebuffer& fb, const Vec3& position, const Vec2& size,
                           f32 radius, const Color& color, BlendMode mode);

    void draw_circle(Framebuffer& fb, const Vec3& position, f32 radius,
                     const Color& color, BlendMode mode);

    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color);

    void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                               const Mat4& view, const Mat4& proj, const Color& color);

    // Effect renderers — drawn before the main shape
    void draw_shadow(Framebuffer& fb, const RenderNode& node);
    void draw_glow(Framebuffer& fb, const RenderNode& node);
};

} // namespace chronon3d
