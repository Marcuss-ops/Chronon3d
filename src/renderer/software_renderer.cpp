#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/scene.hpp>
#include <tracy/Tracy.hpp>
#include <hwy/highway.h>
#include <cmath>
#include <algorithm>

namespace hn = hwy::HWY_NAMESPACE;

namespace chronon3d {

// ---------------------------------------------------------------------------
// Internal helpers (file-scope)
// ---------------------------------------------------------------------------

// Rounded-rect SDF pixel test: returns true if (px,py) is inside a rounded rect
// centered at (cx,cy) with half-extents (hw,hh) and corner radius r.
static bool in_rounded_rect(f32 px, f32 py, f32 cx, f32 cy, f32 hw, f32 hh, f32 r) {
    const f32 qx = std::max(std::abs(px - cx) - (hw - r), 0.0f);
    const f32 qy = std::max(std::abs(py - cy) - (hh - r), 0.0f);
    return qx * qx + qy * qy <= r * r;
}

// Draws a filled rounded rect at (origin) with given size and radius.
static void rasterize_rounded_rect(
    Framebuffer& fb, const Vec3& origin, f32 hw, f32 hh, f32 r, const Color& color)
{
    const f32 cx = origin.x, cy = origin.y;
    const i32 y_min = std::max(static_cast<i32>(cy - hh), 0);
    const i32 y_max = std::min(static_cast<i32>(cy + hh), fb.height());
    const i32 x_min = std::max(static_cast<i32>(cx - hw), 0);
    const i32 x_max = std::min(static_cast<i32>(cx + hw), fb.width());

    for (i32 py = y_min; py < y_max; ++py) {
        for (i32 px = x_min; px < x_max; ++px) {
            if (in_rounded_rect(static_cast<f32>(px), static_cast<f32>(py), cx, cy, hw, hh, r))
                fb.set_pixel(px, py, compositor::blend(color, fb.get_pixel(px, py), BlendMode::Normal));
        }
    }
}

// Draws one effect layer (shadow/glow) with the node's shape expanded by `spread`
// pixels, centered on `origin`, blended with `layer_color`.
static void draw_effect_layer(
    Framebuffer& fb, const RenderNode& node, const Vec3& origin,
    f32 spread, const Color& layer_color)
{
    switch (node.shape.type) {
        case ShapeType::RoundedRect: {
            const f32 hw = node.shape.rounded_rect.size.x * 0.5f + spread;
            const f32 hh = node.shape.rounded_rect.size.y * 0.5f + spread;
            const f32 r  = std::min(node.shape.rounded_rect.radius + spread, std::min(hw, hh));
            rasterize_rounded_rect(fb, origin, hw, hh, r, layer_color);
            break;
        }
        case ShapeType::Rect: {
            const i32 cx = static_cast<i32>(origin.x);
            const i32 cy = static_cast<i32>(origin.y);
            const i32 hw = static_cast<i32>(node.shape.rect.size.x * 0.5f + spread);
            const i32 hh = static_cast<i32>(node.shape.rect.size.y * 0.5f + spread);
            const i32 y0 = std::max(cy - hh, 0), y1 = std::min(cy + hh, fb.height());
            const i32 x0 = std::max(cx - hw, 0), x1 = std::min(cx + hw, fb.width());
            for (i32 y = y0; y < y1; ++y)
                for (i32 x = x0; x < x1; ++x)
                    fb.set_pixel(x, y, compositor::blend(layer_color, fb.get_pixel(x, y), BlendMode::Normal));
            break;
        }
        case ShapeType::Circle: {
            const f32 r  = node.shape.circle.radius + spread;
            const f32 r2 = r * r;
            const i32 ri = static_cast<i32>(r);
            const i32 cx = static_cast<i32>(origin.x);
            const i32 cy = static_cast<i32>(origin.y);
            for (i32 y = cy - ri; y <= cy + ri; ++y) {
                for (i32 x = cx - ri; x <= cx + ri; ++x) {
                    if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height()) continue;
                    const f32 dx = static_cast<f32>(x - cx);
                    const f32 dy = static_cast<f32>(y - cy);
                    if (dx * dx + dy * dy <= r2)
                        fb.set_pixel(x, y, compositor::blend(layer_color, fb.get_pixel(x, y), BlendMode::Normal));
                }
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------------------
// render_frame / render_scene
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

    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    const Mat4 proj  = camera.projection_matrix(aspect);
    const Mat4 view  = camera.view_matrix();

    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;

        const Color linear_color = node.color.to_linear();

        // Draw order: shadow → glow → shape
        if (node.shadow.enabled) draw_shadow(*fb, node);
        if (node.glow.enabled)   draw_glow(*fb, node);

        switch (node.shape.type) {
            case ShapeType::Mesh:
                if (node.mesh)
                    render_mesh_wireframe(*fb, *node.mesh,
                        node.world_transform.to_matrix(), view, proj, linear_color);
                break;
            case ShapeType::Rect:
                draw_rect(*fb, node.world_transform.position,
                    node.shape.rect.size, linear_color, BlendMode::Normal);
                break;
            case ShapeType::RoundedRect:
                draw_rounded_rect(*fb, node.world_transform.position,
                    node.shape.rounded_rect.size, node.shape.rounded_rect.radius,
                    linear_color, BlendMode::Normal);
                break;
            case ShapeType::Circle:
                draw_circle(*fb, node.world_transform.position,
                    node.shape.circle.radius, linear_color, BlendMode::Normal);
                break;
            case ShapeType::Line:
                draw_line(*fb, node.world_transform.position, node.shape.line.to, linear_color);
                break;
            default: break;
        }
    }

    return fb;
}

// ---------------------------------------------------------------------------
// Mesh wireframe
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Primitive rasterizers
// ---------------------------------------------------------------------------

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

void SoftwareRenderer::draw_rect(
    Framebuffer& fb, const Vec3& position, const Vec2& size,
    const Color& color, BlendMode mode)
{
    ZoneScoped;
    const i32 cx = static_cast<i32>(position.x), cy = static_cast<i32>(position.y);
    const i32 hw = static_cast<i32>(size.x * 0.5f), hh = static_cast<i32>(size.y * 0.5f);
    const i32 y_min = std::max(cy - hh, 0), y_max = std::min(cy + hh, fb.height());
    const i32 x_min = std::max(cx - hw, 0), x_max = std::min(cx + hw, fb.width());
    if (x_min >= x_max || y_min >= y_max) return;

    if (mode == BlendMode::Normal && color.a >= 1.0f) {
        for (i32 y = y_min; y < y_max; ++y)
            std::fill(fb.pixels_row(y) + x_min, fb.pixels_row(y) + x_max, color);
        return;
    }

    // SIMD blend: out = A + inv_a * dst
    const f32 inv_a = 1.0f - color.a;
    const f32 A[4]  = {color.r * color.a, color.g * color.a, color.b * color.a, color.a};
    const hn::ScalableTag<f32> d;
    const size_t N = hn::Lanes(d);
    alignas(64) f32 src_tile[64];
    for (size_t i = 0; i < N && i < 64; ++i) src_tile[i] = A[i % 4];
    const auto vA = hn::LoadU(d, src_tile), vInvA = hn::Set(d, inv_a);
    const size_t floats = static_cast<size_t>(x_max - x_min) * 4;

    for (i32 y = y_min; y < y_max; ++y) {
        f32* row = reinterpret_cast<f32*>(fb.pixels_row(y) + x_min);
        size_t i = 0;
        for (; i + N <= floats; i += N)
            hn::StoreU(hn::MulAdd(vInvA, hn::LoadU(d, row + i), vA), d, row + i);
        for (; i < floats; ++i)
            row[i] = A[i % 4] + inv_a * row[i];
    }
}

void SoftwareRenderer::draw_rounded_rect(
    Framebuffer& fb, const Vec3& position, const Vec2& size,
    f32 radius, const Color& color, BlendMode mode)
{
    ZoneScoped;
    const f32 cx = position.x, cy = position.y;
    const f32 hw = size.x * 0.5f, hh = size.y * 0.5f;
    const f32 r  = std::min(radius, std::min(hw, hh));

    const i32 y_min = std::max(static_cast<i32>(cy - hh), 0);
    const i32 y_max = std::min(static_cast<i32>(cy + hh), fb.height());
    const i32 x_min = std::max(static_cast<i32>(cx - hw), 0);
    const i32 x_max = std::min(static_cast<i32>(cx + hw), fb.width());
    if (x_min >= x_max || y_min >= y_max) return;

    for (i32 py = y_min; py < y_max; ++py) {
        for (i32 px = x_min; px < x_max; ++px) {
            if (in_rounded_rect(static_cast<f32>(px), static_cast<f32>(py), cx, cy, hw, hh, r))
                fb.set_pixel(px, py, compositor::blend(color, fb.get_pixel(px, py), mode));
        }
    }
}

void SoftwareRenderer::draw_circle(
    Framebuffer& fb, const Vec3& position, f32 radius,
    const Color& color, BlendMode mode)
{
    ZoneScoped;
    const i32 cx = static_cast<i32>(position.x), cy = static_cast<i32>(position.y);
    const i32 r  = static_cast<i32>(radius);
    const f32 r2 = radius * radius;
    // Closed [cy-r, cy+r] bounding box: fill predicate dx²+dy² <= r² must test
    // pixels at exactly distance r (e.g. circle poles).
    for (i32 y = cy - r; y <= cy + r; ++y) {
        for (i32 x = cx - r; x <= cx + r; ++x) {
            if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height()) continue;
            const f32 dx = static_cast<f32>(x - cx), dy = static_cast<f32>(y - cy);
            if (dx * dx + dy * dy <= r2)
                fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), mode));
        }
    }
}

// ---------------------------------------------------------------------------
// Effect renderers
// ---------------------------------------------------------------------------

void SoftwareRenderer::draw_shadow(Framebuffer& fb, const RenderNode& node) {
    ZoneScoped;
    if (node.shadow.color.a <= 0.0f) return;

    const Color base = node.shadow.color.to_linear();
    const Vec3 origin{
        node.world_transform.position.x + node.shadow.offset.x,
        node.world_transform.position.y + node.shadow.offset.y,
        node.world_transform.position.z
    };

    // Draw outermost → innermost so inner layers paint over outer ones.
    // Quadratic alpha falloff: soft at the edges, solid at the core.
    constexpr int LAYERS = 6;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t     = static_cast<f32>(i) / LAYERS;
        const f32 spread = node.shadow.radius * t;
        const f32 alpha  = base.a * (1.0f - t * t);
        if (alpha > 0.0f)
            draw_effect_layer(fb, node, origin, spread, {base.r, base.g, base.b, alpha});
    }
    // Solid core at zero spread
    draw_effect_layer(fb, node, origin, 0.0f, {base.r, base.g, base.b, base.a * 0.7f});
}

void SoftwareRenderer::draw_glow(Framebuffer& fb, const RenderNode& node) {
    ZoneScoped;
    if (node.glow.intensity <= 0.0f || node.glow.color.a <= 0.0f) return;

    const Color base   = node.glow.color.to_linear();
    const Vec3& origin = node.world_transform.position;

    // Draw outermost → innermost; inner layers are brighter and closest to shape.
    // Linear alpha falloff * intensity.
    constexpr int LAYERS = 5;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 expand = node.glow.radius * t;
        const f32 alpha  = base.a * node.glow.intensity * (1.0f - t);
        if (alpha > 0.0f)
            draw_effect_layer(fb, node, origin, expand, {base.r, base.g, base.b, alpha});
    }
}

} // namespace chronon3d
