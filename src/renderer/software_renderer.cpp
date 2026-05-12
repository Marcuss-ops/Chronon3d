#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <hwy/highway.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <fmt/format.h>

namespace hn = hwy::HWY_NAMESPACE;

namespace chronon3d {

namespace {

raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f) {
    Vec2 size{0, 0};
    switch (shape.type) {
        case ShapeType::Rect: size = shape.rect.size; break;
        case ShapeType::RoundedRect: size = shape.rounded_rect.size; break;
        case ShapeType::Circle: size = Vec2{shape.circle.radius * 2, shape.circle.radius * 2}; break;
        case ShapeType::Image: size = shape.image.size; break;
        default: break;
    }

    if (size.x == 0 && size.y == 0) return {0, 0, 0, 0};

    // Expand size by spread
    Vec2 min_local{-spread, -spread};
    Vec2 max_local{size.x + spread, size.y + spread};

    Vec4 corners[4] = {
        model * Vec4(min_local.x, min_local.y, 0, 1),
        model * Vec4(max_local.x, min_local.y, 0, 1),
        model * Vec4(max_local.x, max_local.y, 0, 1),
        model * Vec4(min_local.x, max_local.y, 0, 1)
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
        static_cast<i32>(std::ceil(max_x)) + 1,
        static_cast<i32>(std::ceil(max_y)) + 1
    };
}

bool hit_test(const Shape& s, Vec2 p, f32 spread = 0.0f) {
    // Note: p is in local space [0, size]
    switch (s.type) {
        case ShapeType::Rect: {
            f32 hw = s.rect.size.x * 0.5f + spread;
            f32 hh = s.rect.size.y * 0.5f + spread;
            f32 px = p.x - s.rect.size.x * 0.5f;
            f32 py = p.y - s.rect.size.y * 0.5f;
            return px >= -hw && px < hw && py >= -hh && py < hh;
        }
        case ShapeType::RoundedRect: {
            f32 hw = s.rounded_rect.size.x * 0.5f + spread;
            f32 hh = s.rounded_rect.size.y * 0.5f + spread;
            f32 px = p.x - s.rounded_rect.size.x * 0.5f;
            f32 py = p.y - s.rounded_rect.size.y * 0.5f;
            f32 r = std::min(s.rounded_rect.radius + spread, std::min(hw, hh));
            // For rounded corners, we use <= for the circle arc but < for the body
            if (std::abs(px) < (hw - r) || std::abs(py) < (hh - r)) {
                return std::abs(px) < hw && std::abs(py) < hh;
            }
            f32 qx = std::abs(px) - (hw - r);
            f32 qy = std::abs(py) - (hh - r);
            return qx * qx + qy * qy < r * r; // Use < to be safe
        }
        case ShapeType::Circle:
            return raster::point_in_circle(p.x - s.circle.radius, p.y - s.circle.radius, s.circle.radius + spread);
        case ShapeType::Image: {
            f32 hw = s.image.size.x * 0.5f + spread;
            f32 hh = s.image.size.y * 0.5f + spread;
            f32 px = p.x - s.image.size.x * 0.5f;
            f32 py = p.y - s.image.size.y * 0.5f;
            return px >= -hw && px < hw && py >= -hh && py < hh;
        }
        default: return false;
    }
}

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color, f32 spread = 0.0f) {
    if (color.a <= 0.0f) return;
    
    raster::BBox bbox = compute_world_bbox(shape, model, spread);
    bbox.clip_to(fb.width(), fb.height());

    if (bbox.is_empty()) return;

    Mat4 inv_model = glm::inverse(model);

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            Vec4 lp = inv_model * Vec4(static_cast<f32>(x), static_cast<f32>(y), 0.0f, 1.0f);
            if (hit_test(shape, Vec2(lp.x, lp.y), spread)) {
                fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), BlendMode::Normal));
            }
        }
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

    RenderState root_state{Mat4(1.0f), 1.0f};

    // 1. Root nodes
    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        RenderState state = combine(root_state, node.world_transform);
        draw_node(*fb, node, state, camera, width, height);
    }

    // 2. Layers — 2D in insertion order, 3D projected and depth-sorted.
    //
    // Draw order rules:
    //   root nodes first
    //   2D layers in insertion order (camera has no effect)
    //   3D layers sorted by depth (farther first, nearer on top)
    //   no z-buffer; intersecting planes are painter's-algorithm only
    const auto& cam25 = scene.camera_2_5d();

    if (!cam25.enabled) {
        for (const auto& layer : scene.layers()) {
            if (!layer.visible) continue;
            RenderState layer_state = combine(root_state, layer.transform);
            for (const auto& node : layer.nodes) {
                if (!node.visible) continue;
                RenderState node_state = combine(layer_state, node.world_transform);
                draw_node(*fb, node, node_state, camera, width, height);
            }
        }
        return fb;
    }

    struct LayerRenderItem {
        const Layer* layer{nullptr};
        Transform    projected_transform{};
        f32          depth{0.0f};
        usize        insertion_index{0};
    };

    std::vector<LayerRenderItem> three_d_layers;
    usize index = 0;

    for (const auto& layer : scene.layers()) {
        if (!layer.visible) { ++index; continue; }

        if (!layer.is_3d) {
            RenderState layer_state = combine(root_state, layer.transform);
            for (const auto& node : layer.nodes) {
                if (!node.visible) continue;
                RenderState node_state = combine(layer_state, node.world_transform);
                draw_node(*fb, node, node_state, camera, width, height);
            }
        } else {
            auto projected = project_layer_2_5d(
                layer.transform, cam25,
                static_cast<f32>(width), static_cast<f32>(height));
            three_d_layers.push_back({&layer, projected.transform, projected.depth, index});
        }
        ++index;
    }

    // Farther layers first → nearer layers paint on top.
    std::stable_sort(three_d_layers.begin(), three_d_layers.end(),
        [](const LayerRenderItem& a, const LayerRenderItem& b) {
            return a.depth > b.depth;
        });

    for (const auto& item : three_d_layers) {
        RenderState layer_state = combine(root_state, item.projected_transform);
        for (const auto& node : item.layer->nodes) {
            if (!node.visible) continue;
            RenderState node_state = combine(layer_state, node.world_transform);
            draw_node(*fb, node, node_state, camera, width, height);
        }
    }

    return fb;
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state, const Camera& camera, i32 width, i32 height) {
    const Color linear_color = node.color.to_linear();
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    // Effects (Shadow/Glow)
    if (node.shadow.enabled) draw_shadow(fb, node, state);
    if (node.glow.enabled)   draw_glow(fb, node, state);

    if (node.shape.type == ShapeType::Mesh) {
        if (node.mesh) {
            const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
            render_mesh_wireframe(fb, *node.mesh, model, camera.view_matrix(), camera.projection_matrix(aspect), linear_color);
        }
        return;
    }

    if (node.shape.type == ShapeType::Line) {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(node.shape.line.to, 1);
        Color col = linear_color;
        col.a *= opacity;
        draw_line(fb, Vec3(p0.x, p0.y, p0.z), Vec3(p1.x, p1.y, p1.z), col);
        return;
    }

    if (node.shape.type == ShapeType::Text) {
        // TextRenderer currently doesn't support full matrix, but it needs correct position and opacity
        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity = opacity;
        m_text_renderer.draw_text(node.shape.text, text_tr, fb);
        return;
    }

    if (node.shape.type == ShapeType::Image) {
        m_image_renderer.draw_image(node.shape.image, state, fb);
        return;
    }

    // Main shape
    Color fill_color = linear_color;
    fill_color.a *= opacity;
    draw_transformed_shape(fb, node.shape, model, fill_color);

    if (diagnostic_) {
        draw_diagnostic_info(fb, node, state);
    }
}

void SoftwareRenderer::draw_diagnostic_info(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (!diagnostic_) return;

    TextStyle debug_style;
    debug_style.font_path = "assets/fonts/Inter-Regular.ttf";
    debug_style.size = 12.0f;
    debug_style.color = Color{1, 0, 0, 0.8f};

    TextShape debug_text;
    debug_text.text = fmt::format("{}: ({:.1f}, {:.1f})", std::string(node.name), state.matrix[3][0], state.matrix[3][1]);
    debug_text.style = debug_style;

    Transform text_tr;
    text_tr.position = Vec3(state.matrix[3]);
    m_text_renderer.draw_text(debug_text, text_tr, fb);
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

void SoftwareRenderer::draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    ZoneScoped;
    if (node.shadow.color.a <= 0.0f) return;

    const Color base = node.shadow.color.to_linear();
    const Mat4& base_model = state.matrix;
    Mat4 shadow_model = math::translate(Vec3(node.shadow.offset.x, node.shadow.offset.y, 0)) * base_model;

    constexpr int LAYERS = 6;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 spread = node.shadow.radius * t;
        const f32 alpha  = base.a * (1.0f - t * t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread);
    }
    draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, base.a * 0.7f * state.opacity}, 0.0f);
}

void SoftwareRenderer::draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    ZoneScoped;
    if (node.glow.intensity <= 0.0f || node.glow.color.a <= 0.0f) return;

    const Color base = node.glow.color.to_linear();
    const Mat4& model = state.matrix;

    constexpr int LAYERS = 5;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 expand = node.glow.radius * t;
        const f32 alpha  = base.a * node.glow.intensity * (1.0f - t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, model, {base.r, base.g, base.b, alpha}, expand);
    }
}

} // namespace chronon3d
