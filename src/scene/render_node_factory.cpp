#include <chronon3d/scene/render_node_factory.hpp>

#include <utility>

namespace chronon3d {

RenderNode RenderNodeFactory::base(std::pmr::memory_resource* res, std::string name) {
    RenderNode node(res);
    node.name = std::pmr::string{std::move(name), res};
    return node;
}

RenderNode RenderNodeFactory::rect(std::pmr::memory_resource* res, std::string name, const RectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Rect;
    node.shape.rect.size = p.size;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
    return node;
}

RenderNode RenderNodeFactory::rounded_rect(std::pmr::memory_resource* res, std::string name, const RoundedRectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::RoundedRect;
    node.shape.rounded_rect.size = p.size;
    node.shape.rounded_rect.radius = p.radius;
    node.corner_radius = p.radius;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
    return node;
}

RenderNode RenderNodeFactory::circle(std::pmr::memory_resource* res, std::string name, const CircleParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Circle;
    node.shape.circle.radius = p.radius;
    node.world_transform.anchor = {p.radius, p.radius, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
    return node;
}

RenderNode RenderNodeFactory::line(std::pmr::memory_resource* res, std::string name, const LineParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Line;
    node.shape.line.to = p.to - p.from;
    node.shape.line.thickness = p.thickness;
    node.shape.line.stroke.trim_start = p.stroke.trim_start;
    node.shape.line.stroke.trim_end = p.stroke.trim_end;
    node.world_transform.position = p.from;
    node.world_transform.anchor = {0, 0, 0};
    node.color = p.color;
    node.fill = Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::path(std::pmr::memory_resource* res, std::string name, PathParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Path;
    node.shape.path.commands = std::move(p.commands);
    node.shape.path.stroke = p.stroke;
    node.shape.path.fill = p.fill;
    node.shape.path.closed = p.closed;
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill;
    return node;
}

RenderNode RenderNodeFactory::image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Image;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.fit = p.fit;
    node.shape.image.focal_point = p.focal_point;
    node.shape.image.crop = p.crop;
    node.shape.image.opacity = p.opacity;
    node.shape.image.radius = p.radius;
    node.corner_radius = p.radius;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

RenderNode RenderNodeFactory::tiled_image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::TiledImage;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.opacity = p.opacity;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {0, 0, 0};
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

RenderNode RenderNodeFactory::grid_background(std::pmr::memory_resource* res, std::string name, const GridBackgroundParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::GridBackground;
    node.shape.grid_background.size = p.size;
    node.shape.grid_background.offset = p.offset;
    node.shape.grid_background.bg_color = p.bg_color;
    node.shape.grid_background.grid_color = p.grid_color;
    node.shape.grid_background.spacing = p.spacing;
    node.shape.grid_background.minor_thickness = p.minor_thickness;
    node.shape.grid_background.major_thickness = p.major_thickness;
    node.shape.grid_background.major_every = p.major_every;
    node.shape.grid_background.centered = p.centered;
    node.world_transform.position = {0.0f, 0.0f, 0.0f};
    node.world_transform.anchor = {0.0f, 0.0f, 0.0f};
    node.color = p.bg_color;
    return node;
}

RenderNode RenderNodeFactory::text(std::pmr::memory_resource* res, std::string name, TextParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Text;
    node.shape.text.text = std::move(p.text);
    node.shape.text.style.font_path = std::move(p.font_path);
    node.shape.text.style.font_family = std::move(p.font_family);
    node.shape.text.style.font_weight = p.font_weight;
    node.shape.text.style.font_style = std::move(p.font_style);
    node.shape.text.style.size = p.font_size;
    node.shape.text.style.color = p.color;
    node.shape.text.style.align = p.align;
    node.shape.text.style.vertical_align = p.vertical_align;
    node.shape.text.style.line_height = p.line_height;
    node.shape.text.style.tracking = p.tracking;
    node.shape.text.style.box_style = p.box_style;
    node.shape.text.style.paint = p.paint;
    node.shape.text.style.shadows = std::move(p.shadows);

    node.shape.text.style.auto_fit = p.auto_fit;
    node.shape.text.style.auto_scale = p.auto_fit;
    node.shape.text.style.max_lines = p.max_lines;
    node.shape.text.style.ellipsis = p.ellipsis;
    node.shape.text.style.min_size = p.min_font_size;
    node.shape.text.style.max_size = p.max_font_size;
    node.shape.text.style.overflow = p.overflow;
    node.shape.text.style.wrap = p.wrap;

    node.shape.text.box.enabled = true;
    node.shape.text.box.size = p.size;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = Fill::solid_color(p.color);
    return node;
}

} // namespace chronon3d
