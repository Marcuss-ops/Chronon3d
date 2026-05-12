#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <string>

namespace chronon3d {

struct RectParams {
    Vec2 size{100, 100};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
};

struct RoundedRectParams {
    Vec2 size{100, 100};
    f32 radius{8.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
};

struct CircleParams {
    f32 radius{50.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
};

struct LineParams {
    Vec3 from{0, 0, 0};
    Vec3 to{100, 0, 0};
    f32 thickness{1.0f};
    Color color{1, 1, 1, 1};
};

struct TextParams {
    std::string content;
    TextStyle style;
    Vec3 pos{0, 0, 0};
};

class SceneBuilder {
public:
    explicit SceneBuilder(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : scene_(res) {}

    SceneBuilder& rect(std::string name, RectParams p) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Rect;
        node.shape.rect.size = p.size;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
        node.color = p.color;
        scene_.add_node(std::move(node));
        return *this;
    }

    SceneBuilder& rounded_rect(std::string name, RoundedRectParams p) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::RoundedRect;
        node.shape.rounded_rect.size = p.size;
        node.shape.rounded_rect.radius = p.radius;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
        node.color = p.color;
        scene_.add_node(std::move(node));
        return *this;
    }

    SceneBuilder& circle(std::string name, CircleParams p) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Circle;
        node.shape.circle.radius = p.radius;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {0, 0, 0}; // Circle center is origin
        node.color = p.color;
        scene_.add_node(std::move(node));
        return *this;
    }

    SceneBuilder& line(std::string name, LineParams p) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Line;
        node.shape.line.to = p.to;
        node.shape.line.thickness = p.thickness;
        node.world_transform.position = p.from;
        node.color = p.color;
        scene_.add_node(std::move(node));
        return *this;
    }

    SceneBuilder& text(std::string name, TextParams p) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Text;
        node.shape.text.text = std::move(p.content);
        node.shape.text.style = p.style;
        node.world_transform.position = p.pos;
        node.color = p.style.color;
        scene_.add_node(std::move(node));
        return *this;
    }

    // Fluent API for transformations
    SceneBuilder& at(Vec3 pos) {
        scene_.last_node().world_transform.position = pos;
        return *this;
    }

    SceneBuilder& rotate(Vec3 euler_deg) {
        scene_.last_node().world_transform.rotation = math::from_euler(euler_deg);
        return *this;
    }

    SceneBuilder& scale(Vec3 s) {
        scene_.last_node().world_transform.scale = s;
        return *this;
    }

    SceneBuilder& anchor(Vec3 a) {
        scene_.last_node().world_transform.anchor = a;
        return *this;
    }

    SceneBuilder& opacity(f32 a) {
        scene_.last_node().color.a = a;
        return *this;
    }

    // Effects
    SceneBuilder& with_shadow(DropShadow shadow) {
        scene_.last_node().shadow = shadow;
        return *this;
    }

    SceneBuilder& with_glow(Glow glow) {
        scene_.last_node().glow = glow;
        return *this;
    }

    // Legacy Overloads (Deprecated)
    [[deprecated("Use rect(id, RectParams)")]]
    SceneBuilder& rect(std::string name, Vec3 position, Color color, Vec2 size = {100, 100}) {
        return rect(std::move(name), {.size = size, .color = color, .pos = position});
    }

    [[deprecated("Use rounded_rect(id, RoundedRectParams)")]]
    SceneBuilder& rounded_rect(std::string name, Vec3 position, Vec2 size, f32 radius, Color color) {
        return rounded_rect(std::move(name), {.size = size, .radius = radius, .color = color, .pos = position});
    }

    [[deprecated("Use circle(id, CircleParams)")]]
    SceneBuilder& circle(std::string name, Vec3 position, f32 radius, Color color) {
        return circle(std::move(name), {.radius = radius, .color = color, .pos = position});
    }

    [[deprecated("Use text(id, TextParams)")]]
    SceneBuilder& text(std::string name, std::string content, Vec3 position, const TextStyle& style) {
        return text(std::move(name), {.content = std::move(content), .style = style, .pos = position});
    }

    [[deprecated("Use line(id, LineParams)")]]
    SceneBuilder& line(std::string name, Vec3 start, Vec3 end, Color color, f32 thickness = 1.0f) {
        return line(std::move(name), {.from = start, .to = end, .thickness = thickness, .color = color});
    }

    [[nodiscard]] Scene build() {
        return std::move(scene_);
    }

private:
    Scene scene_;
};

} // namespace chronon3d
