// Shape method implementations for LayerBuilder.
// Extracted from layer_builder.cpp to reduce file size.

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace chronon3d {

// ── Layer3DDelegate ───────────────────────────────────────────────────

void Layer3DDelegate::add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p, FontEngine* font_engine) {
    auto* res = layer.nodes.get_allocator().resource();
    RenderNode node(res);
    node.name = std::pmr::string{name, res};
    node.shape.type = ShapeType::FakeBox3D;
    node.shape.fake_box3d.world_pos  = p.pos;
    node.shape.fake_box3d.size       = p.size;
    node.shape.fake_box3d.depth      = p.depth;
    node.shape.fake_box3d.color      = p.color;
    node.shape.fake_box3d.top_tint   = p.top_tint;
    node.shape.fake_box3d.side_tint  = p.side_tint;
    node.world_transform.position    = {0, 0, 0};
    node.world_transform.anchor      = {0.0f, 0.0f, 0.0f};
    node.color = p.color;
    node.font_engine = font_engine;
    layer.nodes.push_back(std::move(node));
}

void Layer3DDelegate::add_grid_plane(Layer& layer, std::string name, GridPlaneParams p, FontEngine* font_engine) {
    auto* res = layer.nodes.get_allocator().resource();
    RenderNode node(res);
    node.name = std::pmr::string{name, res};
    node.shape.type = ShapeType::GridPlane;
    node.shape.grid_plane.world_pos      = p.pos;
    node.shape.grid_plane.axis           = p.axis;
    node.shape.grid_plane.extent         = p.extent;
    node.shape.grid_plane.spacing        = p.spacing;
    node.shape.grid_plane.color          = p.color;
    node.shape.grid_plane.fade_distance  = p.fade_distance;
    node.shape.grid_plane.fade_min_alpha = p.fade_min_alpha;
    node.world_transform.position        = {0, 0, 0};
    node.color = p.color;
    node.font_engine = font_engine;
    layer.nodes.push_back(std::move(node));
}

// ── 2D Shapes ─────────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::rect(std::string name, RectParams p) {
    return shape(registry::shape_ids::Rect, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::rounded_rect(std::string name, RoundedRectParams p) {
    return shape(registry::shape_ids::RoundedRect, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::circle(std::string name, CircleParams p) {
    return shape(registry::shape_ids::Circle, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::line(std::string name, LineParams p) {
    return shape(registry::shape_ids::Line, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::path(std::string name, PathParams p) {
    return shape(registry::shape_ids::Path, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::arrow(std::string name, ArrowParams p) {
    return path(std::move(name), make_arrow(p));
}

LayerBuilder& LayerBuilder::star(std::string name, StarParams p) {
    return path(std::move(name), make_star(p));
}

LayerBuilder& LayerBuilder::badge(std::string name, BadgeParams p) {
    return path(std::move(name), make_badge(p));
}

LayerBuilder& LayerBuilder::speech_bubble(std::string name, SpeechBubbleParams p) {
    return path(std::move(name), make_speech_bubble(p));
}

LayerBuilder& LayerBuilder::callout(std::string name, CalloutParams p) {
    return path(std::move(name), make_callout(p));
}

LayerBuilder& LayerBuilder::progress_bar(std::string name, ProgressBarParams p) {
    PathParams bg_path;
    bg_path.commands = make_rounded_rect_commands({0.0f, 0.0f}, p.size, p.corner_radius);
    bg_path.fill = p.background_style.fill;
    bg_path.stroke = p.background_style.stroke;
    bg_path.pos = p.pos;
    bg_path.closed = true;
    path(name + "_bg", bg_path);

    if (p.progress > 0.0f) {
        PathParams fg_path;
        f32 fg_w = p.size.x * std::clamp(p.progress, 0.0f, 1.0f);
        Vec2 fg_center{ -p.size.x * 0.5f + fg_w * 0.5f, 0.0f };
        fg_path.commands = make_rounded_rect_commands(fg_center, {fg_w, p.size.y}, p.corner_radius);
        fg_path.fill = p.fill_style.fill;
        fg_path.stroke = p.fill_style.stroke;
        fg_path.pos = p.pos;
        fg_path.closed = true;
        if (p.color != Color{1,1,1,1}) {
            fg_path.color = p.color;
        }
        path(name, fg_path);
    }
    return *this;
}

LayerBuilder& LayerBuilder::timeline_bar(std::string name, TimelineBarParams p) {
    PathParams bg_path;
    bg_path.commands = make_rounded_rect_commands({0.0f, 0.0f}, p.size, p.corner_radius);
    bg_path.fill = p.background_style.fill;
    bg_path.stroke = p.background_style.stroke;
    bg_path.pos = p.pos;
    bg_path.closed = true;
    path(name + "_bg", bg_path);

    f32 s = std::clamp(p.start, 0.0f, 1.0f);
    f32 e = std::clamp(p.end, 0.0f, 1.0f);
    if (e > s) {
        PathParams fg_path;
        f32 fg_w = p.size.x * (e - s);
        f32 start_pos = -p.size.x * 0.5f + s * p.size.x;
        Vec2 fg_center{ start_pos + fg_w * 0.5f, 0.0f };
        fg_path.commands = make_rounded_rect_commands(fg_center, {fg_w, p.size.y}, p.corner_radius);
        fg_path.fill = p.fill_style.fill;
        fg_path.stroke = p.fill_style.stroke;
        fg_path.pos = p.pos;
        fg_path.closed = true;
        if (p.color != Color{1,1,1,1}) {
            fg_path.color = p.color;
        }
        path(name, fg_path);
    }
    return *this;
}

// ── Content Shapes ────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::image(std::string name, ImageParams p) {
    return shape(registry::shape_ids::Image, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::tiled_image(std::string name, ImageParams p) {
    return shape(registry::shape_ids::TiledImage, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::grid_background(std::string name, GridBackgroundParams p) {
    return shape(registry::shape_ids::GridBackground, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::text(std::string name, TextParams p) {
    return shape(registry::shape_ids::Text, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::shape(std::string_view id, std::string name, registry::ShapeParams params) {
    m_layer.nodes.push_back(registry::ShapeRegistry::instance().create_node(
        id,
        m_layer.nodes.get_allocator().resource(),
        std::move(name),
        std::move(params)
    ));
    m_layer.nodes.back().font_engine = m_font_engine;
    return *this;
}

// ── 3D Shapes (Delegated) ─────────────────────────────────────────────

LayerBuilder& LayerBuilder::fake_box3d(std::string name, FakeBox3DParams p) {
    Layer3DDelegate::add_fake_box3d(m_layer, name, p, m_font_engine);
    return *this;
}

LayerBuilder& LayerBuilder::grid_plane(std::string name, GridPlaneParams p) {
    Layer3DDelegate::add_grid_plane(m_layer, name, p, m_font_engine);
    return *this;
}

// ── Convenience Shapes ────────────────────────────────────────────────

LayerBuilder& LayerBuilder::fullscreen_rect(std::string name, Color color) {
    return rect(std::move(name), {
        .size = { m_screen_width, m_screen_height },
        .color = color,
        .pos = { 0.0f, 0.0f, 0.0f }
    });
}

LayerBuilder& LayerBuilder::fill(Color color) {
    return fullscreen_rect("fill", color);
}

} // namespace chronon3d
