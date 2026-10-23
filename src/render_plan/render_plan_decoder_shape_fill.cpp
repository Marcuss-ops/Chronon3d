#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {

graphics::GradientMesh decode_gradient_mesh(const nlohmann::json& value) {
    graphics::GradientMesh mesh;
    const auto& nodes = value.at("nodes");
    if (!nodes.is_array() || nodes.empty() || nodes.size() > 256)
        throw std::runtime_error("gradient mesh requires between 1 and 256 nodes");
    mesh.nodes.reserve(nodes.size());
    for (const auto& raw_node : nodes) {
        if (!raw_node.is_object() || !raw_node.contains("position") ||
            !raw_node.contains("color"))
            throw std::runtime_error("gradient mesh nodes require position and color");
        const auto& position = raw_node.at("position");
        const auto& color = raw_node.at("color");
        if (!position.is_array() || position.size() != 2 ||
            !color.is_array() || color.size() != 4)
            throw std::runtime_error("gradient mesh node position and color must contain two and four values");
        graphics::GradientMeshNode node;
        node.position = {position[0].get<float>(), position[1].get<float>()};
        node.color = Color{color[0].get<float>(), color[1].get<float>(),
                           color[2].get<float>(), color[3].get<float>()};
        node.weight = raw_node.value("weight", 1.0f);
        const auto bounded = [](float component) {
            return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
        };
        if (!bounded(node.position.x) || !bounded(node.position.y) ||
            !bounded(node.color.r) || !bounded(node.color.g) ||
            !bounded(node.color.b) || !bounded(node.color.a) ||
            !std::isfinite(node.weight) || node.weight < 0.0001f || node.weight > 1000.0f)
            throw std::runtime_error("gradient mesh positions and RGBA must be in [0, 1]; weights must be in [0.0001, 1000]");
        mesh.nodes.push_back(node);
    }
    return mesh;
}

GradientAnimationPlan decode_gradient_animation(const nlohmann::json& value) {
    GradientAnimationPlan animation;
    animation.easing = value.value("easing", std::string{"linear"});
    animation.bezier_curve = decode_bezier_curve(
        value, animation.easing, "fill_gradient_animation");
    for (const auto& key_value : value.at("keyframes")) {
        GradientKeyframePlan key;
        key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
        key.value = decode_gradient_definition(key_value.at("value"));
        animation.keyframes.push_back(std::move(key));
    }
    return animation;
}

}  // namespace

void decode_shape_fill_and_stroke(
    LayerPlan& layer, ShapeContentPlan& shape, const nlohmann::json& raw_shape) {
    const auto finite_vec2 = [](const Vec2& point) {
        return std::isfinite(point.x) && std::isfinite(point.y);
    };
        if (raw_shape.contains("fill")) {
            const auto& fill = raw_shape.at("fill");
            if (fill.is_array()) {
                if (fill.size() != 4)
                    throw std::runtime_error("shape.fill must contain RGBA");
                shape.fill_color = std::array<float, 4>{
                    fill[0].get<float>(), fill[1].get<float>(),
                    fill[2].get<float>(), fill[3].get<float>()};
            } else if (fill.is_object()) {
                if (fill.value("type", std::string{}) == "mesh") {
                    for (auto it = fill.begin(); it != fill.end(); ++it)
                        if (it.key() != "type" && it.key() != "mesh")
                            throw std::runtime_error("mesh fill accepts only type and mesh");
                    if (fill.contains("nodes"))
                        throw std::runtime_error("mesh fill nodes must be nested under fill.mesh.nodes");
                    if (!fill.contains("mesh"))
                        throw std::runtime_error("mesh fill requires a mesh object");
                    const bool closed_shape = shape.type == ShapeContentTypePlan::Rect ||
                        shape.type == ShapeContentTypePlan::RoundedRect ||
                        shape.type == ShapeContentTypePlan::Ellipse ||
                        shape.type == ShapeContentTypePlan::Star ||
                        shape.type == ShapeContentTypePlan::Polygon ||
                        shape.type == ShapeContentTypePlan::Arrow ||
                        shape.type == ShapeContentTypePlan::DotGrid;
                    const bool closed_path = shape.type == ShapeContentTypePlan::Path &&
                        !shape.svg_asset && !shape.lottie_asset &&
                        std::any_of(shape.path.begin(), shape.path.end(), [](const auto& command) {
                            return command.type == ShapePathCommandTypePlan::Close;
                        });
                    if (!closed_shape && !closed_path)
                        throw std::runtime_error("mesh fills require a closed filled shape or closed authored path");
                    shape.gradient_mesh = decode_gradient_mesh(fill.at("mesh"));
                } else if (fill.value("type", std::string{}) == "field") {
                    for (auto it = fill.begin(); it != fill.end(); ++it)
                        if (it.key() != "type" && it.key() != "field" && it.key() != "field_ramp" &&
                            it.key() != "field_render_scale" && it.key() != "field_drift")
                            throw std::runtime_error("field fill accepts only field, ramp, render scale, and drift");
                    if (!fill.contains("field"))
                        throw std::runtime_error("field fill requires a field object");
                    if (shape.type == ShapeContentTypePlan::Line ||
                        shape.type == ShapeContentTypePlan::Arc ||
                        shape.type == ShapeContentTypePlan::Grid ||
                        shape.type == ShapeContentTypePlan::DotGrid ||
                        shape.type == ShapeContentTypePlan::DeviceFrame ||
                        raw_shape.contains("asset") || raw_shape.contains("lottie"))
                        throw std::runtime_error("field fill requires a closed procedural shape or authored path");
                    if (fill.contains("mesh"))
                        throw std::runtime_error("field fill cannot contain a mesh payload");
                    shape.field = decode_field2d_plan(fill.at("field"));
                    if (fill.contains("field_ramp"))
                        shape.field_ramp = decode_gradient_definition(fill.at("field_ramp"));
                    if (fill.contains("field_render_scale")) {
                        shape.field_render_scale = fill.at("field_render_scale").get<std::uint32_t>();
                        if (shape.field_render_scale != 1 && shape.field_render_scale != 2 &&
                            shape.field_render_scale != 4)
                            throw std::runtime_error("field fill render scale must be 1, 2, or 4");
                    }
                    if (fill.contains("field_drift")) {
                        const auto& drift = fill.at("field_drift");
                        if (!drift.is_array() || drift.size() != 2)
                            throw std::runtime_error("field fill drift must contain two values");
                        const Vec2 decoded{drift[0].get<float>(), drift[1].get<float>()};
                        if (!std::isfinite(decoded.x) || !std::isfinite(decoded.y) ||
                            std::abs(decoded.x) > 100.f || std::abs(decoded.y) > 100.f)
                            throw std::runtime_error("field fill drift components must be finite and within [-100, 100]");
                        shape.field_drift = decoded;
                    }
                } else {
                    shape.gradient = decode_gradient_definition(fill);
                }
            }
        }
        if (shape.gradient && (shape.field || shape.gradient_mesh))
            throw std::runtime_error("gradient fill cannot be combined with Field2D or Gradient Mesh");
        if (shape.gradient_mesh && shape.field)
            throw std::runtime_error("Gradient Mesh cannot be combined with Field2D");
        if (shape.type == ShapeContentTypePlan::DotGrid && !shape.fill_color)
            throw std::runtime_error("dot_grid shapes require a solid RGBA fill");
        if (shape.gradient) {
            const auto valid_stop = [](float position, float alpha) {
                return std::isfinite(position) && position >= 0.0f && position <= 1.0f &&
                       std::isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f;
            };
            if (shape.gradient->color_stops.size() < 2)
                throw std::runtime_error("gradient requires at least two color stops");
            for (const auto& stop : shape.gradient->color_stops) {
                if (!valid_stop(stop.position, stop.color.a) ||
                    !std::isfinite(stop.color.r) || !std::isfinite(stop.color.g) ||
                    !std::isfinite(stop.color.b) || stop.color.r < 0.0f || stop.color.r > 1.0f ||
                    stop.color.g < 0.0f || stop.color.g > 1.0f ||
                    stop.color.b < 0.0f || stop.color.b > 1.0f)
                    throw std::runtime_error("gradient color stops must be finite and in [0, 1]");
            }
            for (const auto& stop : shape.gradient->opacity_stops) {
                if (!valid_stop(stop.position, stop.opacity))
                    throw std::runtime_error("gradient opacity stops must be finite and in [0, 1]");
            }
            const auto& gradient = *shape.gradient;
            if (!finite_vec2(gradient.start) || !finite_vec2(gradient.end) ||
                !finite_vec2(gradient.center) || !std::isfinite(gradient.radius) ||
                gradient.radius <= 0.0f || !std::isfinite(gradient.angle))
                throw std::runtime_error("gradient geometry must be finite with positive radius");
        }
        if (raw_shape.contains("stroke")) {
            const auto& stroke = raw_shape.at("stroke");
            StrokeStyle decoded_stroke;
            decoded_stroke.color = "#FFFFFF";
            if (stroke.contains("color")) {
                const auto& stroke_color = stroke.at("color");
                if (stroke_color.is_array()) {
                    if (stroke_color.size() != 4)
                        throw std::runtime_error("shape.stroke.color must contain RGBA");
                    decoded_stroke.solid_color = std::array<float, 4>{
                        stroke_color[0].get<float>(), stroke_color[1].get<float>(),
                        stroke_color[2].get<float>(), stroke_color[3].get<float>()};
                } else if (stroke_color.is_string()) {
                    decoded_stroke.color = stroke_color.get<std::string>();
                } else if (stroke_color.is_object()) {
                    decoded_stroke.gradient = decode_gradient_definition(stroke_color);
                }
            }
            if (stroke.contains("width")) decoded_stroke.width = stroke.at("width").get<float>();
            if (stroke.contains("trim_start")) decoded_stroke.trim_start = stroke.at("trim_start").get<float>();
            if (stroke.contains("trim_end")) decoded_stroke.trim_end = stroke.at("trim_end").get<float>();
            if (stroke.contains("dash_array")) {
                const auto& dash = stroke.at("dash_array");
                if (!dash.is_array()) throw std::runtime_error("stroke.dash_array must be an array");
                for (const auto& entry : dash) {
                    const float value = entry.get<float>();
                    if (!std::isfinite(value) || value < 0.0f)
                        throw std::runtime_error("stroke.dash_array entries must be finite and >= 0");
                    decoded_stroke.dash_array.push_back(value);
                }
            }
            if (stroke.contains("dash_offset")) {
                decoded_stroke.dash_offset = stroke.at("dash_offset").get<float>();
                if (!std::isfinite(decoded_stroke.dash_offset))
                    throw std::runtime_error("stroke.dash_offset must be finite");
            }
            decoded_stroke.cap = stroke.value("cap", decoded_stroke.cap);
            decoded_stroke.join = stroke.value("join", decoded_stroke.join);
            decoded_stroke.miter_limit = stroke.value("miter_limit", decoded_stroke.miter_limit);
            if (stroke.contains("brush")) {
                if (shape.type != ShapeContentTypePlan::Line &&
                    shape.type != ShapeContentTypePlan::Path &&
                    shape.type != ShapeContentTypePlan::Star &&
                    shape.type != ShapeContentTypePlan::Polygon &&
                    shape.type != ShapeContentTypePlan::Arrow &&
                    shape.type != ShapeContentTypePlan::Arc &&
                    shape.type != ShapeContentTypePlan::Grid &&
                    shape.type != ShapeContentTypePlan::DotGrid)
                    throw std::runtime_error("brush strokes require a path-backed shape");
                if (shape.svg_asset || shape.lottie_asset)
                    throw std::runtime_error("brush strokes are not supported on imported asset paths");
                const auto& raw_brush = stroke.at("brush");
                graphics::BrushProfile brush;
                brush.size = raw_brush.value("size", 0.0f);
                const bool has_brush_size = raw_brush.contains("size") && brush.size > 0.0f;
                brush.hardness = raw_brush.value("hardness", brush.hardness);
                brush.opacity = raw_brush.value("opacity", brush.opacity);
                brush.flow = raw_brush.value("flow", brush.flow);
                brush.opacity_start = raw_brush.value("opacity_start", brush.opacity_start);
                brush.opacity_end = raw_brush.value("opacity_end", brush.opacity_end);
                brush.bleed_radius = raw_brush.value("bleed_radius", brush.bleed_radius);
                brush.edge_density = raw_brush.value("edge_density", brush.edge_density);
                brush.spacing = raw_brush.value("spacing", brush.spacing);
                brush.roundness = raw_brush.value("roundness", brush.roundness);
                brush.angle = raw_brush.value("angle", brush.angle);
                brush.taper_start = raw_brush.value("taper_start", brush.taper_start);
                brush.taper_end = raw_brush.value("taper_end", brush.taper_end);
                brush.pressure_width = raw_brush.value("pressure_width", brush.pressure_width);
                brush.pressure_opacity = raw_brush.value("pressure_opacity", brush.pressure_opacity);
                brush.width_jitter = raw_brush.value("width_jitter", brush.width_jitter);
                brush.opacity_jitter = raw_brush.value("opacity_jitter", brush.opacity_jitter);
                brush.angle_jitter = raw_brush.value("angle_jitter", brush.angle_jitter);
                brush.scatter = raw_brush.value("scatter", brush.scatter);
                brush.grain_strength = raw_brush.value("grain", brush.grain_strength);
                brush.grain_scale = raw_brush.value("grain_scale", brush.grain_scale);
                brush.edge_noise = raw_brush.value("edge_noise", brush.edge_noise);
                brush.edge_softness = raw_brush.value("edge_softness", brush.edge_softness);
                brush.seed = raw_brush.value("seed", brush.seed);
                if (raw_brush.contains("texture"))
                    brush.texture = decode_field2d_plan(raw_brush.at("texture"));
                if (raw_brush.contains("along_path_gradient")) {
                    brush.along_path_gradient = decode_gradient_definition(raw_brush.at("along_path_gradient"));
                    const auto& gradient = *brush.along_path_gradient;
                    if (gradient.type != graphics::GradientType::Linear ||
                        gradient.color_stops.size() < 2 ||
                        std::any_of(gradient.color_stops.begin(), gradient.color_stops.end(), [](const auto& stop) {
                            return !std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                                !std::isfinite(stop.color.r) || !std::isfinite(stop.color.g) ||
                                !std::isfinite(stop.color.b) || !std::isfinite(stop.color.a) ||
                                stop.color.r < 0.0f || stop.color.r > 1.0f || stop.color.g < 0.0f || stop.color.g > 1.0f ||
                                stop.color.b < 0.0f || stop.color.b > 1.0f || stop.color.a < 0.0f || stop.color.a > 1.0f;
                        }) ||
                        std::any_of(gradient.opacity_stops.begin(), gradient.opacity_stops.end(), [](const auto& stop) {
                            return !std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                                !std::isfinite(stop.opacity) || stop.opacity < 0.0f || stop.opacity > 1.0f;
                        }))
                        throw std::runtime_error("brush along-path gradient stops must be finite and in [0, 1]");
                }
                if (raw_brush.contains("pressure_curve")) {
                    const auto& curve = raw_brush.at("pressure_curve");
                    if (!curve.is_array() || curve.size() > 256)
                        throw std::runtime_error("stroke.brush.pressure_curve must contain at most 256 points");
                    for (const auto& point : curve) {
                        if (!point.is_array() || point.size() != 2)
                            throw std::runtime_error("stroke.brush.pressure_curve points must be [position, pressure]");
                        brush.pressure_curve.push_back({point[0].get<float>(), point[1].get<float>()});
                    }
                }
                if (!brush.valid())
                    throw std::runtime_error("shape.stroke.brush parameters are outside the supported range");
                if (!has_brush_size && (!decoded_stroke.width ||
                    !std::isfinite(*decoded_stroke.width) || *decoded_stroke.width <= 0.0f))
                    throw std::runtime_error("brush stroke requires an explicit positive width when brush.size is zero");
                decoded_stroke.brush = std::move(brush);
            }
            decoded_stroke.taper_start = stroke.value("taper_start", 1.0f);
            decoded_stroke.taper_end = stroke.value("taper_end", 1.0f);
            if (decoded_stroke.brush && (stroke.contains("taper_start") || stroke.contains("taper_end")))
                throw std::runtime_error("brush taper must be authored in stroke.brush");
            if ((decoded_stroke.cap != "butt" && decoded_stroke.cap != "round" &&
                 decoded_stroke.cap != "square") ||
                (decoded_stroke.join != "miter" && decoded_stroke.join != "round" &&
                 decoded_stroke.join != "bevel") ||
                !std::isfinite(decoded_stroke.miter_limit) || decoded_stroke.miter_limit < 1.0f ||
                decoded_stroke.miter_limit > 1000.0f ||
                !std::isfinite(decoded_stroke.taper_start) || decoded_stroke.taper_start < 0.0f ||
                decoded_stroke.taper_start > 1.0f || !std::isfinite(decoded_stroke.taper_end) ||
                decoded_stroke.taper_end < 0.0f || decoded_stroke.taper_end > 1.0f)
                throw std::runtime_error("shape.stroke cap, join, or taper is outside the supported range");
            if (stroke.contains("gradient"))
                decoded_stroke.gradient = decode_gradient_definition(stroke.at("gradient"));
            if (decoded_stroke.brush && stroke.contains("color") && stroke.at("color").is_object())
                throw std::runtime_error("brush stroke color must be a solid color; use brush.along_path_gradient for a color ramp");
            const bool closed_stroke_path =
                shape.type == ShapeContentTypePlan::Star ||
                shape.type == ShapeContentTypePlan::Polygon ||
                shape.type == ShapeContentTypePlan::Arrow ||
                shape.type == ShapeContentTypePlan::DotGrid ||
                ((shape.type == ShapeContentTypePlan::Line ||
                  shape.type == ShapeContentTypePlan::Path) &&
                 std::any_of(shape.path.begin(), shape.path.end(), [](const auto& command) {
                    return command.type == ShapePathCommandTypePlan::Close;
                }));
            if (closed_stroke_path &&
                ((decoded_stroke.brush &&
                  (decoded_stroke.brush->taper_start != 1.0f ||
                   decoded_stroke.brush->taper_end != 1.0f)) ||
                 (!decoded_stroke.brush &&
                  (decoded_stroke.taper_start != 1.0f ||
                   decoded_stroke.taper_end != 1.0f))))
                throw std::runtime_error("tapered strokes require an open path");
            if (decoded_stroke.brush && decoded_stroke.gradient)
                throw std::runtime_error("brush strokes use brush.along_path_gradient or brush opacity, not a competing stroke gradient");
            if (decoded_stroke.gradient) {
                const auto& gradient = *decoded_stroke.gradient;
                if (gradient.color_stops.size() < 2)
                    throw std::runtime_error("stroke gradient requires at least two color stops");
                for (const auto& stop : gradient.color_stops) {
                    if (!std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                        !std::isfinite(stop.color.r) || !std::isfinite(stop.color.g) ||
                        !std::isfinite(stop.color.b) || !std::isfinite(stop.color.a) ||
                        stop.color.r < 0.0f || stop.color.r > 1.0f ||
                        stop.color.g < 0.0f || stop.color.g > 1.0f ||
                        stop.color.b < 0.0f || stop.color.b > 1.0f ||
                        stop.color.a < 0.0f || stop.color.a > 1.0f)
                        throw std::runtime_error("stroke gradient color stops must be finite and in [0, 1]");
                }
                for (const auto& stop : gradient.opacity_stops) {
                    if (!std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                        !std::isfinite(stop.opacity) || stop.opacity < 0.0f || stop.opacity > 1.0f)
                        throw std::runtime_error("stroke gradient opacity stops must be finite and in [0, 1]");
                }
                if (!finite_vec2(gradient.start) || !finite_vec2(gradient.end) ||
                    !finite_vec2(gradient.center) || !std::isfinite(gradient.radius) ||
                    gradient.radius <= 0.0f || !std::isfinite(gradient.angle))
                    throw std::runtime_error("stroke gradient geometry must be finite with positive radius");
            }
            shape.stroke = std::move(decoded_stroke);
        }
        if (shape.fill_color) {
            for (const auto component : *shape.fill_color) {
                if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
                    throw std::runtime_error("shape fill RGBA must be finite and in [0, 1]");
            }
            // Normalize both names to the same solid paint value. The explicit
            // shape-local fill wins over the legacy layer.color alias above.
            layer.color = *shape.fill_color;
        }
        if (shape.stroke) {
            if (shape.stroke->color.empty() && shape.stroke->solid_color)
                shape.stroke->color = "#FFFFFF";
            if (shape.stroke->solid_color && shape.stroke->gradient)
                throw std::runtime_error("shape stroke cannot specify both solid color and gradient");
            if (shape.stroke->color.empty() && shape.stroke->gradient)
                shape.stroke->color = "#FFFFFF";
            if (shape.stroke->width &&
                (!std::isfinite(*shape.stroke->width) || *shape.stroke->width < 0.0f))
                throw std::runtime_error("shape stroke width must be finite and non-negative");
            if (shape.stroke->trim_start &&
                (!std::isfinite(*shape.stroke->trim_start) || *shape.stroke->trim_start < 0.0f ||
                 *shape.stroke->trim_start > 1.0f))
                throw std::runtime_error("shape stroke trim_start must be finite and in [0, 1]");
            if (shape.stroke->trim_end &&
                (!std::isfinite(*shape.stroke->trim_end) || *shape.stroke->trim_end < 0.0f ||
                 *shape.stroke->trim_end > 1.0f))
                throw std::runtime_error("shape stroke trim_end must be finite and in [0, 1]");
            if (shape.stroke->solid_color) {
                for (const auto component : *shape.stroke->solid_color) {
                    if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
                        throw std::runtime_error("shape stroke RGBA must be finite and in [0, 1]");
                }
            }
        }

}

void decode_shape_fill_gradient_animation(
    LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("fill_gradient_animation")) {
        if (layer.animation && std::any_of(layer.animation->tracks.begin(),
                layer.animation->tracks.end(), [](const AnimationTrackPlan& track) {
                    return track.property == "fill_color";
                }))
            throw std::runtime_error(
                "fill_color and fill_gradient_animation tracks are mutually exclusive");
        if (layer.type != LayerType::Shape || !layer.shape ||
            layer.shape->type == ShapeContentTypePlan::Line ||
            layer.shape->type == ShapeContentTypePlan::Path ||
            layer.shape->type == ShapeContentTypePlan::Arc ||
            layer.shape->type == ShapeContentTypePlan::Grid ||
            !layer.shape->gradient) {
            throw std::runtime_error(
                "fill_gradient_animation requires a filled rect, rounded_rect, or ellipse shape");
        }
        auto animation = decode_gradient_animation(value.at("fill_gradient_animation"));
        if (animation.keyframes.empty())
            throw std::runtime_error("fill_gradient_animation requires keyframes");
        const auto& base = *layer.shape->gradient;
        const auto compatible = [&](const graphics::GradientDefinition& candidate) {
            return candidate.type == base.type && candidate.spread == base.spread;
        };
        for (const auto& key : animation.keyframes) {
            if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                !compatible(key.value))
                throw std::runtime_error(
                    "fill gradient keyframes must use the base gradient type and spread");
        }
        std::stable_sort(animation.keyframes.begin(), animation.keyframes.end(),
            [](const auto& a, const auto& b) { return a.frame < b.frame; });
        for (std::size_t index = 1; index < animation.keyframes.size(); ++index)
            if (animation.keyframes[index - 1].frame == animation.keyframes[index].frame)
                throw std::runtime_error("fill gradient keyframe frames must be unique");
        layer.fill_gradient_animation = std::move(animation);
    }
}


}  // namespace chronon3d::render_plan::detail
