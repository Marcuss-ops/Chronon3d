#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <taskflow/taskflow.hpp>
#include <hwy/highway.h>

namespace chronon3d {

class SoftwareRenderer : public Renderer {
public:
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) override {
        Scene scene = comp.evaluate(frame);
        return render_scene(scene, comp.camera, comp.width(), comp.height());
    }

    std::unique_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera, i32 width, i32 height) override {
        auto fb = std::make_unique<Framebuffer>(width, height);
        fb->clear(Color::black());

        f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
        Mat4 proj = camera.projection_matrix(aspect);
        Mat4 view = camera.view_matrix();

        for (const auto& node : scene.nodes()) {
            if (!node.visible) continue;

            if (node.mesh) {
                render_mesh_wireframe(*fb, *node.mesh, node.world_transform.to_matrix(), view, proj, node.color);
            } else {
                draw_rect(*fb, node.world_transform, node.color, BlendMode::Normal);
            }
        }

        return fb;
    }

private:
    void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& proj, const Color& color) {
        Mat4 mvp = proj * view * model;
        const auto& vertices = mesh.vertices();
        const auto& indices = mesh.indices();
        
        std::vector<Vec3> projected_vertices(vertices.size());
        
        // Vertex Projection Loop (Hot Path)
        for (usize i = 0; i < vertices.size(); ++i) {
            Vec4 p = mvp * Vec4(vertices[i].position, 1.0f);
            if (p.w != 0.0f) {
                p.x /= p.w; p.y /= p.w; p.z /= p.w;
            }
            f32 x = (p.x + 1.0f) * 0.5f * fb.width();
            f32 y = (1.0f - (p.y + 1.0f) * 0.5f) * fb.height();
            projected_vertices[i] = {x, y, p.z};
        }

        // Rasterization Loop
        for (usize i = 0; i < indices.size(); i += 3) {
            const Vec3& p1 = projected_vertices[indices[i]];
            const Vec3& p2 = projected_vertices[indices[i+1]];
            const Vec3& p3 = projected_vertices[indices[i+2]];

            draw_line(fb, p1, p2, color);
            draw_line(fb, p2, p3, color);
            draw_line(fb, p3, p1, color);
        }
    }

    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color) {
        i32 x0 = static_cast<i32>(p1.x);
        i32 y0 = static_cast<i32>(p1.y);
        i32 x1 = static_cast<i32>(p2.x);
        i32 y1 = static_cast<i32>(p2.y);

        i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        i32 err = dx + dy, e2;

        while (true) {
            fb.set_pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void draw_rect(Framebuffer& fb, const Transform& transform, const Color& color, BlendMode mode) {
        i32 cx = static_cast<i32>(transform.position.x);
        i32 cy = static_cast<i32>(transform.position.y);
        i32 hw = 50, hh = 50;
        for (i32 y = cy - hh; y < cy + hh; ++y) {
            for (i32 x = cx - hw; x < cx + hw; ++x) {
                if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height()) continue;
                fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), mode));
            }
        }
    }
};

} // namespace chronon3d
