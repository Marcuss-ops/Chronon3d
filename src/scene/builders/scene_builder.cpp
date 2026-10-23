#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/layout/layout_solver.hpp>

namespace chronon3d {

SceneBuilder& SceneBuilder::set_evaluated_camera(Camera2_5D camera) {
    scene_.set_camera_2_5d(std::move(camera));
    return *this;
}

SceneBuilder& SceneBuilder::ambient_light(Color color, f32 intensity) {
    scene_.light_context().enabled = true;
    scene_.light_context().ambient_enabled = true;
    scene_.light_context().ambient_color = color;
    scene_.light_context().ambient = intensity;
    return *this;
}

SceneBuilder& SceneBuilder::apply_lighting_rig(const rendering::LightingRig& rig) {
    rig.apply_to(scene_.light_context(), &scene_.rim_light());
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

SceneBuilder& SceneBuilder::point_light_source(rendering::PointLightSource source) {
    scene_.light_context().point_lights.push_back(source);
    return *this;
}

SceneBuilder& SceneBuilder::shape(std::string_view id, std::string name, registry::ShapeParams params) {
    scene_.add_node(m_shape_registry->create_node(
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

Scene SceneBuilder::build() {
    LayoutSolver().solve(scene_, m_width, m_height);
    scene_.resolve_hierarchy(current_integer_frame());
    // Sequence V2: aggregate asset manifests from all layers into scene
    for (const auto& layer : scene_.layers()) {
        scene_.asset_manifest().merge(layer.asset_manifest);
    }
    return std::move(scene_);
}

const Camera2_5D& SceneBuilder::camera_2_5d() const {
    return scene_.camera_2_5d();
}

SceneBuilder& SceneBuilder::grid_background(std::string name, GridBackgroundParams p) {
    return shape(registry::shape_ids::GridBackground, std::move(name), std::move(p));
}

SceneBuilder& SceneBuilder::stagger(const StaggerConfig& config, StaggerOrder order) {
    chronon3d::stagger_layers(scene_.layers(), config, order);
    return *this;
}

SceneBuilder& SceneBuilder::stagger(const std::vector<std::string>& names, const StaggerConfig& config, StaggerOrder order) {
    chronon3d::stagger_named_layers(scene_.layers(), names, config, order);
    return *this;
}

// ── A4 — Explicit node handle accessor ───────────────────────────

NodeHandle SceneBuilder::last_node_handle() {
    return NodeHandle(scene_.last_node());
}

SceneBuilder& SceneBuilder::clip_transition(std::string name_a, std::string name_b,
                                            const ClipTransitionSpec& spec,
                                            Frame from, Frame duration) {
    scene_.clip_transitions().push_back(SceneClipTransition{
        .layer_a = std::move(name_a),
        .layer_b = std::move(name_b),
        .spec = spec,
        .from = from,
        .duration = duration,
    });
    return *this;
}

} // namespace chronon3d
