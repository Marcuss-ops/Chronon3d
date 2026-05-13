#include <chronon3d/scene/scene_builder.hpp>
#include <unordered_map>
#include <functional>

namespace chronon3d {

SceneBuilder& SceneBuilder::rect(std::string name, RectParams p) {
    RenderNode node(scene_.resource());
    node.name = std::pmr::string{name, scene_.resource()};
    node.shape.type = ShapeType::Rect;
    node.shape.rect.size = p.size;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    scene_.add_node(std::move(node));
    return *this;
}

SceneBuilder& SceneBuilder::rounded_rect(std::string name, RoundedRectParams p) {
    RenderNode node(scene_.resource());
    node.name = std::pmr::string{name, scene_.resource()};
    node.shape.type = ShapeType::RoundedRect;
    node.shape.rounded_rect.size = p.size;
    node.shape.rounded_rect.radius = p.radius;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    scene_.add_node(std::move(node));
    return *this;
}

SceneBuilder& SceneBuilder::circle(std::string name, CircleParams p) {
    RenderNode node(scene_.resource());
    node.name = std::pmr::string{name, scene_.resource()};
    node.shape.type = ShapeType::Circle;
    node.shape.circle.radius = p.radius;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.radius, p.radius, 0.0f};
    node.color = p.color;
    scene_.add_node(std::move(node));
    return *this;
}

SceneBuilder& SceneBuilder::line(std::string name, LineParams p) {
    RenderNode node(scene_.resource());
    node.name = std::pmr::string{name, scene_.resource()};
    node.shape.type = ShapeType::Line;
    node.shape.line.to = p.to - p.from;
    node.shape.line.thickness = p.thickness;
    node.world_transform.position = p.from;
    node.world_transform.anchor = {0, 0, 0};
    node.color = p.color;
    scene_.add_node(std::move(node));
    return *this;
}

SceneBuilder& SceneBuilder::text(std::string name, TextParams p) {
    RenderNode node(scene_.resource());
    node.name = std::pmr::string{name, scene_.resource()};
    node.shape.type = ShapeType::Text;
    node.shape.text.text  = std::move(p.content);
    node.shape.text.style = p.style;
    node.shape.text.box   = p.box;
    node.world_transform.position = p.pos;
    node.color = p.style.color;
    scene_.add_node(std::move(node));
    return *this;
}

SceneBuilder& SceneBuilder::image(std::string name, ImageParams p) {
    RenderNode node(scene_.resource());
    node.name = std::pmr::string{name, scene_.resource()};
    node.shape.type = ShapeType::Image;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.opacity = p.opacity;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = Color{1, 1, 1, p.opacity};

    scene_.add_node(std::move(node));
    return *this;
}
} // namespace chronon3d
