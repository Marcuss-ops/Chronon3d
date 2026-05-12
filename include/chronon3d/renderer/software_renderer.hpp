#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

namespace chronon3d {

/**
 * SoftwareRenderer implements a CPU-based rasterizer.
 * Coordinate System:
 * - Origin (0,0) is Top-Left.
 * - X increases Right, Y increases Down.
 * - Rect/Circle positions are Center-based.
 * - Line positions are absolute pixel coordinates.
 */
class SoftwareRenderer : public Renderer {
public:
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) override;

    std::unique_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera, i32 width, i32 height) override;

private:
    void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj, const Color& color);

    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color);

    void draw_rect(Framebuffer& fb, const Vec3& position, const Vec2& size, const Color& color, BlendMode mode);

    void draw_circle(Framebuffer& fb, const Vec3& position, f32 radius, const Color& color, BlendMode mode);
};

} // namespace chronon3d
