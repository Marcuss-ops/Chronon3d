#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>

namespace chronon3d {

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

SceneBuilder& SceneBuilder::grid_background(std::string name, GridBackgroundParams p) {
    return shape(registry::shape_ids::GridBackground, std::move(name), std::move(p));
}
} // namespace chronon3d
