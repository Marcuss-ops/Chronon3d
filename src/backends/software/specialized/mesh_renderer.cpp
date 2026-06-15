#include "mesh_renderer.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include "../rasterizers/scanline_rasterizer.hpp"
#include <chronon3d/geometry/mesh.hpp>
#include <vector>

namespace chronon3d {
namespace renderer {

void render_mesh_wireframe(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color)
{
    const Mat4 mvp = proj * view * model;
    const auto& vertices = mesh.vertices();
    const auto& indices  = mesh.indices();

    std::vector<Vec3> projected(vertices.size());
    for (usize i = 0; i < vertices.size(); ++i) {
        Vec4 p = mvp * Vec4(vertices[i].position, 1.0f);
        if (p.w != 0.0f) { p.x /= p.w; p.y /= p.w; }
        projected[i] = {(p.x + 1.0f) * 0.5f * fb.width(),
                        (1.0f - (p.y + 1.0f) * 0.5f) * fb.height(), 0.0f};
    }
    for (usize i = 0; i < indices.size(); i += 3) {
        bline(fb, Vec2(projected[indices[i]]),   Vec2(projected[indices[i+1]]), color);
        bline(fb, Vec2(projected[indices[i+1]]), Vec2(projected[indices[i+2]]), color);
        bline(fb, Vec2(projected[indices[i+2]]), Vec2(projected[indices[i]]),   color);
    }
}

void render_mesh_filled(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color)
{
    const Mat4 mvp = proj * view * model;
    const auto& vertices = mesh.vertices();
    const auto& indices  = mesh.indices();

    // ── Depth buffer for 3D raster scanning ──────────────────────────────
    std::vector<float> depth_buffer_vec(static_cast<size_t>(fb.width()) * fb.height(), 0.0f);
    std::span<float> depth_buffer(depth_buffer_vec);

    std::vector<Vec3> projected(vertices.size());
    for (usize i = 0; i < vertices.size(); ++i) {
        // Screen-space XY
        Vec4 p = mvp * Vec4(vertices[i].position, 1.0f);
        bool ok = (p.w != 0.0f);
        if (ok) { p.x /= p.w; p.y /= p.w; }
        const float sx = (p.x + 1.0f) * 0.5f * fb.width();
        const float sy = (1.0f - (p.y + 1.0f) * 0.5f) * fb.height();

        // Camera-space Z for depth testing: transform to view space
        Vec4 view_p = view * model * Vec4(vertices[i].position, 1.0f);
        const float cam_z = view_p.z;  // positive Z forward in LH

        projected[i] = {sx, sy, cam_z};
    }

    for (usize i = 0; i < indices.size(); i += 3) {
        Vec3 tri[3] = {
            projected[indices[i]],
            projected[indices[i+1]],
            projected[indices[i+2]],
        };
        fill_triangle(fb, tri, color, depth_buffer);
    }
}

} // namespace renderer
} // namespace chronon3d
