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

    SceneBuilder& rect(std::string name, Vec3 position, Color color, Vec3 scale = {1, 1, 1}, Vec3 rotation_euler = {0, 0, 0}) {
        RenderNode node(scene_.resource());
        node.name = std::move(name);
        node.world_transform.position = position;
        node.world_transform.scale = scale;
        node.world_transform.rotation = math::from_euler(rotation_euler);
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
