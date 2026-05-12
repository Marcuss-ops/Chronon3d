#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <string>

namespace chronon3d {

class SceneBuilder {
public:
    explicit SceneBuilder(std::pmr::memory_resource* res = std::pmr::get_default_resource()) 
        : scene_(res) {}

    SceneBuilder& rect(std::string name, Vec3 position, Color color, Vec2 size = {100, 100}, Vec3 rotation_euler = {0, 0, 0}) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Rect;
        node.shape.rect.size = size;
        node.world_transform.position = position;
        node.world_transform.rotation = math::from_euler(rotation_euler);
        node.color = color;
        scene_.add_node(std::move(node));
        return *this;
    }

    SceneBuilder& line(std::string name, Vec3 start, Vec3 end, Color color, f32 thickness = 1.0f) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Line;
        node.shape.line.to = end;
        node.shape.line.thickness = thickness;
        node.world_transform.position = start;
        node.color = color;
        scene_.add_node(std::move(node));
        return *this;
    }

    SceneBuilder& circle(std::string name, Vec3 position, f32 radius, Color color) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.shape.type = ShapeType::Circle;
        node.shape.circle.radius = radius;
        node.world_transform.position = position;
        node.color = color;
        scene_.add_node(std::move(node));
        return *this;
    }

    [[nodiscard]] Scene build() {
        return std::move(scene_);
    }

private:
    Scene scene_;
};

} // namespace chronon3d
