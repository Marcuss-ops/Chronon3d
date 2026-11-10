#pragma once

#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::graph::detail {

inline void apply_particle_sample(
    RenderNode& node, const compiler::SampledElementParameters& sample) {
    node.visible = sample.active;
    node.world_transform.position = sample.position;
    node.world_transform.rotation = glm::angleAxis(
        sample.rotation.z, Vec3{0.0f, 0.0f, 1.0f});
    // Particle alpha is carried by fill/image paint in the RenderPlan path;
    // keeping it out of the node transform avoids multiplying alpha twice.
    node.world_transform.opacity = 1.0f;
    node.color = sample.color;
    if (node.shape.type() == ShapeType::Circle) {
        const float radius = std::max(0.0f, sample.size * 0.5f);
        node.shape.circle().radius = radius;
        node.world_transform.anchor = Vec3{radius, radius, 0.0f};
        if (node.fill.gradient.type == graphics::GradientType::Radial &&
            node.fill.gradient.color_stops.size() >= 2) {
            node.fill.gradient.color_stops.front().color = sample.color;
            auto transparent = sample.color;
            transparent.a = 0.0f;
            node.fill.gradient.color_stops.back().color = transparent;
        } else {
            node.fill = Fill::solid_color(sample.color);
        }
    } else if (node.shape.type() == ShapeType::Path) {
        const float radius = std::max(0.0f, sample.size * 0.5f);
        const std::size_t segments = radius <= 4.0f ? 16U :
            (radius <= 20.0f ? 32U : 64U);
        auto& path = node.shape.path();
        path.commands.clear();
        path.commands.reserve(segments + 2);
        for (std::size_t segment = 0; segment < segments; ++segment) {
            const float angle = 6.2831853071795864769f *
                static_cast<float>(segment) / static_cast<float>(segments);
            const Vec2 point = Vec2{sample.position.x, sample.position.y} + Vec2{
                std::cos(angle) * radius, std::sin(angle) * radius};
            if (segment == 0) path.commands.push_back(PathCommand::move_to(point));
            else path.commands.push_back(PathCommand::line_to(point));
        }
        path.commands.push_back(PathCommand::close());
        path.fill = Fill::solid_color(sample.color);
        path.fill_rule = PathFillRule::NonZero;
        path.closed = true;
        path.stroke.enabled = false;
        node.fill = path.fill;
        // Path vertices already include the emitter-local position.
        node.world_transform.position = Vec3{0.0f, 0.0f, sample.position.z};
        node.world_transform.rotation = Quat{1.0f, 0.0f, 0.0f, 0.0f};
    } else if (node.shape.type() == ShapeType::Image) {
        const float side = std::max(0.0f, sample.size * sample.sprite_scale);
        node.shape.image().size = Vec2{side, side};
        node.shape.image().opacity = sample.opacity;
        node.shape.image().tint = Color{
            sample.color.r, sample.color.g, sample.color.b, 1.0f};
        node.world_transform.anchor = Vec3{side * 0.5f, side * 0.5f, 0.0f};
    }
}

}  // namespace chronon3d::graph::detail
