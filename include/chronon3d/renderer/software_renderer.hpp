#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <taskflow/taskflow.hpp>
#include <hwy/highway.h>
#include <tracy/Tracy.hpp>

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
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) override {
        Scene scene = comp.evaluate(frame);
        return render_scene(scene, comp.camera, comp.width(), comp.height());
    }

    std::unique_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera, i32 width, i32 height) override {
        ZoneScoped;
        auto fb = std::make_unique<Framebuffer>(width, height);
        fb->clear(Color::black());

        f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
        Mat4 proj = camera.projection_matrix(aspect);
        Mat4 view = camera.view_matrix();

        // Primitives are rendered in insertion order.
        for (const auto& node : scene.nodes()) {
            if (!node.visible) continue;

            Color linear_color = node.color.to_linear();

            switch (node.shape.type) {
                case ShapeType::Mesh:
                    if (node.mesh) {
                        render_mesh_wireframe(*fb, *node.mesh, node.world_transform.to_matrix(), view, proj, linear_color);
                    }
                    break;
                case ShapeType::Rect:
                    draw_rect(*fb, node.world_transform.position, node.shape.rect.size, linear_color, BlendMode::Normal);
                    break;
                case ShapeType::Circle:
                    draw_circle(*fb, node.world_transform.position, node.shape.circle.radius, linear_color, BlendMode::Normal);
                    break;
                case ShapeType::Line:
                    draw_line(*fb, node.world_transform.position, node.shape.line.to, linear_color);
                    break;
                default:
                    break;
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

    /**
     * Bresenham's line algorithm with alpha blending.
     */
    void draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color) {
        ZoneScoped;
        i32 x0 = static_cast<i32>(p1.x);
        i32 y0 = static_cast<i32>(p1.y);
        i32 x1 = static_cast<i32>(p2.x);
        i32 y1 = static_cast<i32>(p2.y);

        i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        i32 err = dx + dy, e2;

        while (true) {
            if (x0 >= 0 && x0 < fb.width() && y0 >= 0 && y0 < fb.height()) {
                fb.set_pixel(x0, y0, compositor::blend(color, fb.get_pixel(x0, y0), BlendMode::Normal));
            }
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void draw_rect(Framebuffer& fb, const Vec3& position, const Vec2& size, const Color& color, BlendMode mode) {
        ZoneScoped;
        i32 cx = static_cast<i32>(position.x);
        i32 cy = static_cast<i32>(position.y);
        i32 hw = static_cast<i32>(size.x * 0.5f);
        i32 hh = static_cast<i32>(size.y * 0.5f);
        for (i32 y = cy - hh; y < cy + hh; ++y) {
            for (i32 x = cx - hw; x < cx + hw; ++x) {
                if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height()) continue;
                fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), mode));
            }
        }
    }

    void draw_circle(Framebuffer& fb, const Vec3& position, f32 radius, const Color& color, BlendMode mode) {
        ZoneScoped;
        i32 cx = static_cast<i32>(position.x);
        i32 cy = static_cast<i32>(position.y);
        i32 r = static_cast<i32>(radius);
        f32 r2 = radius * radius;

        for (i32 y = cy - r; y <= cy + r; ++y) {
            for (i32 x = cx - r; x <= cx + r; ++x) {
                if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height()) continue;
                
                f32 dx = static_cast<f32>(x - cx);
                f32 dy = static_cast<f32>(y - cy);
                if (dx * dx + dy * dy <= r2) {
                    fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), mode));
                }
            }
        }
    }
};

} // namespace chronon3d
