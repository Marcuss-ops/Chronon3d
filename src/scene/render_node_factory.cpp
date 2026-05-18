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
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
    return node;
}

RenderNode RenderNodeFactory::rounded_rect(std::pmr::memory_resource* res, std::string name, const RoundedRectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::RoundedRect;
    node.shape.rounded_rect.size = p.size;
    node.shape.rounded_rect.radius = p.radius;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
    return node;
}

RenderNode RenderNodeFactory::circle(std::pmr::memory_resource* res, std::string name, const CircleParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Circle;
    node.shape.circle.radius = p.radius;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.radius, p.radius, 0.0f};
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

RenderNode RenderNodeFactory::text(std::pmr::memory_resource* res, std::string name, TextParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Text;
    node.shape.text.text = std::move(p.content);
    node.shape.text.style = p.style;
    node.shape.text.box = p.box;
    node.world_transform.position = p.pos;
    node.color = p.style.color;
    return node;
}

RenderNode RenderNodeFactory::image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Image;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.opacity = p.opacity;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

} // namespace chronon3d
