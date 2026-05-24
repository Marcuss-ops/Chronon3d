#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>

namespace chronon3d {

SceneBuilder& SceneBuilder::ambient_light(Color color, f32 intensity) {
    scene_.light_context().enabled = true;
    scene_.light_context().ambient_enabled = true;
    scene_.light_context().ambient_color = color;
    scene_.light_context().ambient = intensity;
    return *this;
}

SceneBuilder& SceneBuilder::directional_light(Vec3 direction, Color color, f32 intensity) {
    scene_.light_context().enabled = true;
    scene_.light_context().directional_enabled = true;
    scene_.light_context().direction =
        glm::length(direction) > 1e-6f ? glm::normalize(direction) : Vec3{0, 0, -1};
    scene_.light_context().directional_color = color;
    scene_.light_context().diffuse = intensity;
    return *this;
}

SceneBuilder& SceneBuilder::shape(std::string_view id, std::string name, registry::ShapeParams params) {
    scene_.add_node(registry::ShapeRegistry::instance().create_node(
        id,
        scene_.resource(),
        std::move(name),
        std::move(params)
    ));
    return *this;
}

SceneBuilder& SceneBuilder::rect(std::string name, RectParams p) {
    return shape(registry::shape_ids::Rect, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::rounded_rect(std::string name, RoundedRectParams p) {
    return shape(registry::shape_ids::RoundedRect, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::circle(std::string name, CircleParams p) {
    return shape(registry::shape_ids::Circle, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::line(std::string name, LineParams p) {
    return shape(registry::shape_ids::Line, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::path(std::string name, PathParams p) {
    return shape(registry::shape_ids::Path, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::image(std::string name, ImageParams p) {
    return shape(registry::shape_ids::Image, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::at(Vec3 pos) {
    scene_.last_node().world_transform.position = pos;
    return *this;
}

SceneBuilder& SceneBuilder::rotate(Vec3 euler_deg) {
    scene_.last_node().world_transform.rotation = math::from_euler(euler_deg);
    return *this;
}

SceneBuilder& SceneBuilder::scale(Vec3 s) {
    scene_.last_node().world_transform.scale = s;
    return *this;
}

SceneBuilder& SceneBuilder::anchor(Vec3 a) {
    scene_.last_node().world_transform.anchor = a;
    return *this;
}

SceneBuilder& SceneBuilder::opacity(f32 a) {
    scene_.last_node().world_transform.opacity = a;
    return *this;
}

SceneBuilder& SceneBuilder::with_shadow(DropShadow shadow) {
    scene_.last_node().shadow = shadow;
    return *this;
}

SceneBuilder& SceneBuilder::with_glow(Glow glow) {
    scene_.last_node().glow = glow;
    return *this;
}

Scene SceneBuilder::build() {
    scene_.resolve_hierarchy(current_frame_);
    return std::move(scene_);
}

const Camera2_5D& SceneBuilder::camera_2_5d() const {
    return scene_.camera_2_5d();
}

} // namespace chronon3d
