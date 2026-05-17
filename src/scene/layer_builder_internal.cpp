#include "layer_builder_internal.hpp"

#include <chronon3d/math/transform.hpp>
#include <utility>

namespace chronon3d::layer_builder_internal {

std::pmr::memory_resource* node_resource(Layer& layer) {
    return layer.nodes.get_allocator().resource();
}

RenderNode& append_node(Layer& layer, std::string name) {
    auto* res = node_resource(layer);
    RenderNode node(res);
    node.name = std::pmr::string{std::move(name), res};
    layer.nodes.push_back(std::move(node));
    return layer.nodes.back();
}

RenderNode* last_node(Layer& layer) {
    if (layer.nodes.empty()) {
        return nullptr;
    }
    return &layer.nodes.back();
}

void append_rect(Layer& layer, std::string name, const RectParams& p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::Rect;
    node.shape.rect.size = p.size;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
}

void append_rounded_rect(Layer& layer, std::string name, const RoundedRectParams& p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::RoundedRect;
    node.shape.rounded_rect.size = p.size;
    node.shape.rounded_rect.radius = p.radius;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
}

void append_circle(Layer& layer, std::string name, const CircleParams& p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::Circle;
    node.shape.circle.radius = p.radius;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.radius, p.radius, 0.0f};
    node.color = p.color;
    node.fill = p.fill.value_or(Fill::solid_color(p.color));
}

void append_line(Layer& layer, std::string name, const LineParams& p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::Line;
    node.shape.line.to = p.to - p.from;
    node.shape.line.thickness = p.thickness;
    node.shape.line.stroke.trim_start = p.stroke.trim_start;
    node.shape.line.stroke.trim_end   = p.stroke.trim_end;
    node.world_transform.position = p.from;
    node.color = p.color;
}

void append_text(Layer& layer, std::string name, TextParams p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::Text;
    node.shape.text.text = std::move(p.content);
    node.shape.text.style = p.style;
    node.shape.text.box = p.box;
    node.world_transform.position = p.pos;
    node.color = p.style.color;
}

void append_image(Layer& layer, std::string name, ImageParams p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::Image;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.opacity = p.opacity;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = Color{1, 1, 1, p.opacity};
}

void set_last_shadow(Layer& layer, DropShadow shadow) {
    if (auto* node = last_node(layer)) {
        node->shadow = shadow;
    }
}

void set_last_glow(Layer& layer, Glow glow) {
    if (auto* node = last_node(layer)) {
        node->glow = glow;
    }
}

void set_last_position(Layer& layer, Vec3 pos) {
    if (auto* node = last_node(layer)) {
        node->world_transform.position = pos;
    }
}

void set_last_rotation(Layer& layer, Vec3 euler_deg) {
    if (auto* node = last_node(layer)) {
        node->world_transform.rotation = math::from_euler(euler_deg);
    }
}

void set_last_scale(Layer& layer, Vec3 s) {
    if (auto* node = last_node(layer)) {
        node->world_transform.scale = s;
    }
}

void set_last_anchor(Layer& layer, Vec3 a) {
    if (auto* node = last_node(layer)) {
        node->world_transform.anchor = a;
    }
}

void set_last_opacity(Layer& layer, f32 opacity) {
    if (auto* node = last_node(layer)) {
        node->world_transform.opacity = opacity;
    }
}

} // namespace chronon3d::layer_builder_internal
