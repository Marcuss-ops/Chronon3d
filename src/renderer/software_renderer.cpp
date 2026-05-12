#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <hwy/highway.h>
#include <cmath>
#include <algorithm>
#include <fmt/format.h>

namespace hn = hwy::HWY_NAMESPACE;

namespace chronon3d {

namespace {

raster::BBox compute_world_bbox(const RenderNode& node, const Mat4& model) {
    Vec2 size{0, 0};
    switch (node.shape.type) {
        case chronon3d::ShapeType::Rect: size = node.shape.rect.size; break;
        case chronon3d::ShapeType::RoundedRect: size = node.shape.rounded_rect.size; break;
        case chronon3d::ShapeType::Circle: size = Vec2{node.shape.circle.radius * 2, node.shape.circle.radius * 2}; break;
        default: break;
    }

    if (size.x == 0 && size.y == 0) return {0, 0, 0, 0};

    // We transform the 4 corners of the local-space rect
    // Since anchor is applied in model matrix, local space is [0, size]
    Vec4 corners[4] = {
        model * Vec4(0, 0, 0, 1),
        model * Vec4(size.x, 0, 0, 1),
        model * Vec4(size.x, size.y, 0, 1),
        model * Vec4(0, size.y, 0, 1)
    };

    f32 min_x = corners[0].x, max_x = corners[0].x;
    f32 min_y = corners[0].y, max_y = corners[0].y;

    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)),
        static_cast<i32>(std::ceil(max_y))
    };
}

bool hit_test(const Shape& s, Vec2 p) {
    // Note: p is in local space [0, size]
    switch (s.type) {
        case chronon3d::ShapeType::Rect:
            return raster::point_in_rect(p.x - s.rect.size.x * 0.5f, p.y - s.rect.size.y * 0.5f, s.rect.size.x * 0.5f, s.rect.size.y * 0.5f);
        case chronon3d::ShapeType::RoundedRect:
            return raster::point_in_rounded_rect(p.x - s.rounded_rect.size.x * 0.5f, p.y - s.rounded_rect.size.y * 0.5f, s.rounded_rect.size.x * 0.5f, s.rounded_rect.size.y * 0.5f, s.rounded_rect.radius);
        case chronon3d::ShapeType::Circle:
            return raster::point_in_circle(p.x - s.circle.radius, p.y - s.circle.radius, s.circle.radius);
        default: return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// SoftwareRenderer
// ---------------------------------------------------------------------------

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp, Frame frame) {
    Scene scene = comp.evaluate(frame);
    return render_scene(scene, comp.camera, comp.width(), comp.height());
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height)
{
    ZoneScoped;
    auto fb = std::make_unique<Framebuffer>(width, height);
    fb->clear(Color::black());

    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;

        const Mat4 model = node.world_transform.to_matrix();
        const Color linear_color = node.color.to_linear();
        const f32 opacity = node.world_transform.opacity;

        // Draw order: shadow → glow → shape
        // For simplicity in Phase 2, we keep shadows/glows as simple offsets if not rotated,
        // or we can skip them for now if rotated.
        if (node.shadow.enabled) draw_shadow(*fb, node);
        if (node.glow.enabled)   draw_glow(*fb, node);

        if (node.shape.type == ShapeType::Mesh) {
            if (node.mesh) {
                const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
                render_mesh_wireframe(*fb, *node.mesh, model, camera.view_matrix(), camera.projection_matrix(aspect), linear_color);
            }
            continue;
        }

        if (node.shape.type == ShapeType::Line) {
            Vec4 p0 = model * Vec4(0, 0, 0, 1);
            Vec4 p1 = model * Vec4(node.shape.line.to, 1);
            Color col = linear_color;
            col.a *= opacity;
            draw_line(*fb, Vec3(p0.x, p0.y, p0.z), Vec3(p1.x, p1.y, p1.z), col);
            continue;
        }

        if (node.shape.type == ShapeType::Text) {
            m_text_renderer.draw_text(node.shape.text, node.world_transform, *fb);
            continue;
        }

        // General shape rendering with Inverse Mapping
        raster::BBox bbox = compute_world_bbox(node, model);
        bbox.clip_to(width, height);

        if (bbox.is_empty()) continue;

        Mat4 inv_model = glm::inverse(model);
        Color fill_color = linear_color;
        fill_color.a *= opacity;

        for (i32 y = bbox.y0; y < bbox.y1; ++y) {
            for (i32 x = bbox.x0; x < bbox.x1; ++x) {
                // Transform pixel center to local space
                Vec4 lp = inv_model * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
                
                if (hit_test(node.shape, Vec2(lp.x, lp.y))) {
                    fb->set_pixel(x, y, compositor::blend(fill_color, fb->get_pixel(x, y), BlendMode::Normal));
                }
            }
        }

        if (diagnostic_) {
            draw_diagnostic_info(*fb, node);
        }
    }

    return fb;
}

void SoftwareRenderer::draw_diagnostic_info(Framebuffer& fb, const RenderNode& node) {
    if (!diagnostic_) return;

    TextStyle debug_style;
    debug_style.font_path = "assets/fonts/Inter-Regular.ttf";
    debug_style.size = 12.0f;
    debug_style.color = Color{1, 0, 0, 0.8f};

    TextShape debug_text;
    debug_text.text = fmt::format("{}: ({:.1f}, {:.1f})", std::string(node.name), node.world_transform.position.x, node.world_transform.position.y);
    debug_text.style = debug_style;

    m_text_renderer.draw_text(debug_text, node.world_transform, fb);
}

void SoftwareRenderer::render_mesh_wireframe(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color)
{
    const Mat4 mvp = proj * view * model;
    const auto& vertices = mesh.vertices();
    const auto& indices  = mesh.indices();

    std::vector<Vec3> projected(vertices.size());
    for (usize i = 0; i < vertices.size(); ++i) {
        Vec4 p = mvp * Vec4(vertices[i].position, 1.0f);
        if (p.w != 0.0f) { p.x /= p.w; p.y /= p.w; p.z /= p.w; }
        projected[i] = {(p.x + 1.0f) * 0.5f * fb.width(),
                        (1.0f - (p.y + 1.0f) * 0.5f) * fb.height(), p.z};
    }
    for (usize i = 0; i < indices.size(); i += 3) {
        draw_line(fb, projected[indices[i]],   projected[indices[i+1]], color);
        draw_line(fb, projected[indices[i+1]], projected[indices[i+2]], color);
        draw_line(fb, projected[indices[i+2]], projected[indices[i]],   color);
    }
}

void SoftwareRenderer::draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color) {
    ZoneScoped;
    i32 x0 = static_cast<i32>(p1.x), y0 = static_cast<i32>(p1.y);
    i32 x1 = static_cast<i32>(p2.x), y1 = static_cast<i32>(p2.y);
    const i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy, e2;
    while (true) {
        if (x0 >= 0 && x0 < fb.width() && y0 >= 0 && y0 < fb.height())
            fb.set_pixel(x0, y0, compositor::blend(color, fb.get_pixel(x0, y0), BlendMode::Normal));
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void SoftwareRenderer::draw_shadow(Framebuffer& fb, const RenderNode& node) {
    // Simplified shadow for Phase 2: just an offset, no rotation/scale for now
    // (Or we could apply the model matrix to the shadow as well, but let's keep it simple)
}

void SoftwareRenderer::draw_glow(Framebuffer& fb, const RenderNode& node) {
    // Simplified glow for Phase 2
}

} // namespace chronon3d
