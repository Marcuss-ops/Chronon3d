#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>

#include <cmath>

namespace chronon3d::compiler {

chronon3d::Scene SceneMaterializer::materialize_scene(
    const CompiledProgram& program, std::pmr::memory_resource* resource) const {
    const auto& meta = program.metadata();
    SceneBuilder scene(resource);
    for (const auto& layer : program.layers()) {
        const std::string name = layer.name.empty()
            ? ("layer_" + std::to_string(layer.id)) : layer.name;
        Transform transform;
        transform.position = {layer.position[0], layer.position[1], layer.position[2]};
        transform.scale = {layer.scale[0], layer.scale[1], layer.scale[2]};
        transform.rotation = glm::quat(glm::radians(Vec3{
            layer.rotation[0], layer.rotation[1], layer.rotation[2]}));
        transform.opacity = layer.opacity;

        scene.layer(name, [&](LayerBuilder& builder) {
            builder.from(Frame{layer.start_frame})
                .duration(Frame{layer.duration_frames})
                .transform(transform)
                .opacity(layer.opacity);
            switch (layer.kind) {
                case NodeKind::Color: {
                    RectParams params;
                    params.size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    params.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                    // RectParams are authored around their local centre. A
                    // full-canvas background has no explicit position, so
                    // place its top-left at the canvas origin exactly as the
                    // render-plan compatibility compiler does.
                    if (layer.position_dimensions < 2) {
                        builder.pin_to(Anchor::Center);
                        params.pos = {-params.size.x * 0.5f, -params.size.y * 0.5f, 0.0f};
                    }
                    builder.rect("color", std::move(params));
                    break;
                }
                case NodeKind::Image: {
                    ImageParams params;
                    params.asset_path = layer.asset;
                    params.size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    params.fit = static_cast<chronon3d::FitMode>(layer.fit);
                    params.radius = layer.radius;
                    builder.image("image", std::move(params));
                    break;
                }
                case NodeKind::Video: {
                    video::VideoSource source{.path = layer.source.empty() ? layer.asset : layer.source};
                    if (layer.size_dimensions == 2) source.size = {layer.size[0], layer.size[1]};
                    if (layer.loop) source.loop_mode = video::VideoLoopMode::Loop;
                    builder.video(std::move(source));
                    break;
                }
                case NodeKind::Text:
                    builder.kind(LayerKind::Text);
                    break;
                case NodeKind::Shape: {
                    const auto shape = layer.shape.value_or(render_plan::ShapeContentPlan{});
                    const Vec2 size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    const auto layer_color = Color{
                        layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                    auto fill_style = [&]() -> std::optional<graphics::FillStyle> {
                        if (shape.gradient) {
                            graphics::FillStyle fill;
                            fill.enabled = true;
                            fill.gradient = *shape.gradient;
                            return fill;
                        }
                        if (shape.fill_color) {
                            return graphics::FillStyle::solid(Color{
                                (*shape.fill_color)[0], (*shape.fill_color)[1],
                                (*shape.fill_color)[2], (*shape.fill_color)[3]});
                        }
                        return std::nullopt;
                    }();
                    graphics::StrokeStyle stroke;
                    if (shape.stroke) {
                        stroke.enabled = true;
                        stroke.width = shape.stroke->width.value_or(1.0f);
                        if (shape.stroke->solid_color) {
                            const auto& rgba = *shape.stroke->solid_color;
                            stroke.color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
                        }
                        if (shape.stroke->gradient) stroke.gradient = shape.stroke->gradient;
                        stroke.trim_start = shape.stroke->trim_start.value_or(0.0f);
                        stroke.trim_end = shape.stroke->trim_end.value_or(1.0f);
                    }
                    switch (shape.type) {
                        case render_plan::ShapeContentTypePlan::Rect: {
                            RectParams params;
                            params.size = size;
                            params.color = layer_color;
                            params.fill = fill_style;
                            params.stroke = stroke;
                            builder.rect("shape", std::move(params));
                            break;
                        }
                        case render_plan::ShapeContentTypePlan::RoundedRect: {
                            RoundedRectParams params;
                            params.size = size;
                            params.radius = shape.radius;
                            params.color = layer_color;
                            params.fill = fill_style;
                            params.stroke = stroke;
                            builder.rounded_rect("shape", std::move(params));
                            break;
                        }
                        case render_plan::ShapeContentTypePlan::Ellipse: {
                            CircleParams params;
                            params.radius = std::min(size.x, size.y) * 0.5f;
                            params.color = layer_color;
                            params.fill = fill_style;
                            params.stroke = stroke;
                            builder.circle("shape", std::move(params));
                            break;
                        }
                        case render_plan::ShapeContentTypePlan::Line:
                        case render_plan::ShapeContentTypePlan::Path: {
                            PathParams params;
                            for (const auto& command : shape.path) {
                                const Vec2 point{command.point[0], command.point[1]};
                                const Vec2 control1{command.control1[0], command.control1[1]};
                                const Vec2 control2{command.control2[0], command.control2[1]};
                                switch (command.type) {
                                    case render_plan::ShapePathCommandTypePlan::MoveTo:
                                        params.commands.push_back(PathCommand::move_to(point)); break;
                                    case render_plan::ShapePathCommandTypePlan::LineTo:
                                        params.commands.push_back(PathCommand::line_to(point)); break;
                                    case render_plan::ShapePathCommandTypePlan::QuadraticTo:
                                        params.commands.push_back(PathCommand::quadratic_to(control1, point)); break;
                                    case render_plan::ShapePathCommandTypePlan::CubicTo:
                                        params.commands.push_back(PathCommand::cubic_to(control1, control2, point)); break;
                                    case render_plan::ShapePathCommandTypePlan::Close:
                                        params.commands.push_back(PathCommand::close());
                                        params.closed = true;
                                        break;
                                }
                            }
                            if (shape.gradient) {
                                params.fill.enabled = true;
                                params.fill.type = fill_type_for_gradient(shape.gradient->type);
                                params.fill.gradient = *shape.gradient;
                            } else if (shape.fill_color) {
                                params.fill = Fill::solid_color(Color{
                                    (*shape.fill_color)[0], (*shape.fill_color)[1],
                                    (*shape.fill_color)[2], (*shape.fill_color)[3]});
                            }
                            params.stroke = stroke.to_path_stroke();
                            if (shape.type == render_plan::ShapeContentTypePlan::Line &&
                                params.stroke.width <= 1.0f) params.stroke.width = 1.0f;
                            params.pos = {0.0f, 0.0f, 0.0f};
                            builder.path("shape", std::move(params));
                            break;
                        }
                    }
                    break;
                }
                case NodeKind::Unknown:
                    break;
            }
        });
    }
    return scene.build();
}

}  // namespace chronon3d::compiler
