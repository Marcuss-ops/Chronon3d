#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

AnimationTrackPlan decode_animation_track(const nlohmann::json& value) {
    AnimationTrackPlan track;
    // 'property' is optional: text-selector bounds (start/end/offset/amount)
    // carry it for backward compatibility but never use it semantically, so a
    // selector track without a property is valid.  Layer-animation and text-
    // animator property tracks require it at the schema level; a missing one
    // surfaces as "unsupported property" during compilation instead of a
    // confusing decode failure.
    track.property = value.value("property", std::string{});
    track.easing = value.value("easing", std::string{"linear"});
    for (const auto& key_value : value.at("keyframes")) {
        AnimationKeyframePlan key;
        key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
        const auto& raw = key_value.at("value");
        if (raw.is_array()) {
            for (const auto& component : raw) key.value.push_back(component.get<float>());
        } else {
            key.value.push_back(raw.get<float>());
        }
        track.keyframes.push_back(std::move(key));
    }
    return track;
}

TextSelectorPlan decode_text_selector(const nlohmann::json& value) {
    TextSelectorPlan selector;
    selector.id = value.value("id", std::string{});
    selector.unit = value.value("unit", std::string{"glyph"});
    selector.shape = value.value("shape", std::string{"smooth"});
    selector.order = value.value("order", std::string{"forward"});
    selector.combine = value.value("combine", std::string{"replace"});
    if (value.contains("start")) selector.start = decode_animation_track(value.at("start"));
    if (value.contains("end")) selector.end = decode_animation_track(value.at("end"));
    if (value.contains("offset")) selector.offset = decode_animation_track(value.at("offset"));
    if (value.contains("amount")) selector.amount = decode_animation_track(value.at("amount"));
    selector.exclude_spaces = value.value("exclude_spaces", true);
    selector.randomize_order = value.value("randomize_order", false);
    selector.random_seed = value.value("random_seed", std::uint64_t{0});
    return selector;
}

TextAnimatorPlan decode_text_animator(const nlohmann::json& value) {
    TextAnimatorPlan animator;
    animator.id = value.value("id", std::string{});
    if (value.contains("selectors")) {
        for (const auto& selector : value.at("selectors"))
            animator.selectors.push_back(decode_text_selector(selector));
    }
    if (value.contains("properties")) {
        for (const auto& property : value.at("properties"))
            animator.properties.push_back(decode_animation_track(property));
    }
    return animator;
}

}  // namespace

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});

    if (value.contains("size")) {
        const auto& size = value.at("size");
        layer.size_dimensions = size.size();
        for (std::size_t i = 0; i < size.size() && i < layer.size.size(); ++i) {
            layer.size[i] = size.at(i).get<float>();
            if (!std::isfinite(layer.size[i]))
                throw std::runtime_error("layer.size components must be finite");
        }
    }
    if (value.contains("color")) {
        const auto& color = value.at("color");
        if (!color.is_array() || color.size() != 4)
            throw std::runtime_error("layer.color must contain RGBA");
        for (std::size_t i = 0; i < layer.color.size(); ++i) {
            layer.color[i] = color.at(i).get<float>();
            if (!std::isfinite(layer.color[i]))
                throw std::runtime_error("layer.color components must be finite");
        }
    }

    if (value.contains("shape")) {
        const auto& raw_shape = value.at("shape");
        ShapeContentPlan shape;
        const auto shape_type = raw_shape.at("type").get<std::string>();
        if (shape_type == "rect") shape.type = ShapeContentTypePlan::Rect;
        else if (shape_type == "rounded_rect") shape.type = ShapeContentTypePlan::RoundedRect;
        else if (shape_type == "ellipse") shape.type = ShapeContentTypePlan::Ellipse;
        else if (shape_type == "line") shape.type = ShapeContentTypePlan::Line;
        else if (shape_type == "path") shape.type = ShapeContentTypePlan::Path;
        else throw std::runtime_error("unsupported shape.type: " + shape_type);
        if (layer.size_dimensions != 2 || !std::isfinite(layer.size[0]) ||
            !std::isfinite(layer.size[1]) || layer.size[0] <= 0.0f || layer.size[1] <= 0.0f)
            throw std::runtime_error("shape layers require positive two-dimensional size");
        shape.radius = raw_shape.value("radius", 0.0f);
        if (!std::isfinite(shape.radius) || shape.radius < 0.0f)
            throw std::runtime_error("shape.radius must be finite and non-negative");
        if (shape.type == ShapeContentTypePlan::RoundedRect) {
            const float max_radius = std::min(layer.size[0], layer.size[1]) * 0.5f;
            if (shape.radius > max_radius)
                throw std::runtime_error(
                    "rounded_rect radius must not exceed half the smaller size dimension");
        }
        if (shape.type == ShapeContentTypePlan::Line && !raw_shape.contains("stroke"))
            throw std::runtime_error("line shapes require a stroke");
        if (raw_shape.contains("fill") && raw_shape.at("fill").is_array()) {
            // Shape-local fill is canonical. Legacy layer.color is only an alias
            // for plans that omit shape.fill; it never overrides explicit fill.
        } else if (raw_shape.contains("fill") && raw_shape.at("fill").is_object()) {
            // Gradient payload is decoded below.
        } else if (value.contains("color") && value.at("color").is_array()) {
            const auto& alias = value.at("color");
            if (alias.size() != 4)
                throw std::runtime_error("legacy layer.color shape alias must contain RGBA");
            shape.fill_color = std::array<float, 4>{
                alias[0].get<float>(), alias[1].get<float>(),
                alias[2].get<float>(), alias[3].get<float>()};
        }
        const auto finite_point = [](const std::array<float, 2>& point) {
            return std::isfinite(point[0]) && std::isfinite(point[1]);
        };
        const auto finite_vec2 = [](const Vec2& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        };
        if (raw_shape.contains("path")) {
            for (const auto& raw_command : raw_shape.at("path")) {
                ShapePathCommandPlan command;
                const auto kind = raw_command.at("type").get<std::string>();
                if (kind == "move_to") command.type = ShapePathCommandTypePlan::MoveTo;
                else if (kind == "line_to") command.type = ShapePathCommandTypePlan::LineTo;
                else if (kind == "quadratic_to") command.type = ShapePathCommandTypePlan::QuadraticTo;
                else if (kind == "cubic_to") command.type = ShapePathCommandTypePlan::CubicTo;
                else if (kind == "close") command.type = ShapePathCommandTypePlan::Close;
                else throw std::runtime_error("unsupported shape path command type: " + kind);
                const auto read_point = [](const nlohmann::json& point,
                                           std::array<float, 2>& output) {
                    if (!point.is_array() || point.size() != 2)
                        throw std::runtime_error("shape path points must have exactly two numbers");
                    output = {point[0].get<float>(), point[1].get<float>()};
                };
                if (raw_command.contains("point")) read_point(raw_command.at("point"), command.point);
                if (raw_command.contains("control1")) read_point(raw_command.at("control1"), command.control1);
                if (raw_command.contains("control2")) read_point(raw_command.at("control2"), command.control2);
                const bool has_point = raw_command.contains("point");
                const bool has_control1 = raw_command.contains("control1");
                const bool has_control2 = raw_command.contains("control2");
                switch (command.type) {
                    case ShapePathCommandTypePlan::MoveTo:
                    case ShapePathCommandTypePlan::LineTo:
                        if (!has_point || has_control1 || has_control2)
                            throw std::runtime_error("move_to/line_to require point only");
                        break;
                    case ShapePathCommandTypePlan::QuadraticTo:
                        if (!has_point || !has_control1 || has_control2)
                            throw std::runtime_error("quadratic_to requires point and control1 only");
                        break;
                    case ShapePathCommandTypePlan::CubicTo:
                        if (!has_point || !has_control1 || !has_control2)
                            throw std::runtime_error("cubic_to requires point, control1, and control2");
                        break;
                    case ShapePathCommandTypePlan::Close:
                        if (has_point || has_control1 || has_control2)
                            throw std::runtime_error("close accepts no coordinates");
                        break;
                }
                shape.path.push_back(command);
            }
        }
        if ((shape.type == ShapeContentTypePlan::Line ||
             shape.type == ShapeContentTypePlan::Path) && shape.path.empty())
            throw std::runtime_error("line and path shapes require path commands");
        if (raw_shape.contains("operators")) {
            const auto& raw_ops = raw_shape.at("operators");
            if (!raw_ops.is_array() || raw_ops.size() > 16)
                throw std::runtime_error("shape.operators must be an array with at most 16 entries");
            for (const auto& raw_op : raw_ops) {
                if (!raw_op.is_object() || !raw_op.contains("kind") || !raw_op.contains("params"))
                    throw std::runtime_error("unsupported path operator");
                const auto& p = raw_op.at("params");
                if (!p.is_object()) throw std::runtime_error("path operator params must be an object");
                const auto kind = raw_op.at("kind").get<std::string>();
                PathOperatorPlan decoded_op;
                if (kind == "repeater") {
                    decoded_op.kind = PathOperatorKindPlan::Repeater;
                    auto& r = decoded_op.repeater;
                    r.copies = p.at("copies").get<std::uint32_t>();
                    const auto read2 = [&](const char* key, std::array<float, 2>& out) {
                        if (!p.contains(key)) return;
                        const auto& a = p.at(key);
                        if (!a.is_array() || a.size() != 2)
                            throw std::runtime_error(std::string("repeater ") + key + " requires two values");
                        out = {a[0].get<float>(), a[1].get<float>()};
                    };
                    read2("position", r.position);
                    read2("scale", r.scale);
                    read2("anchor", r.anchor);
                    r.rotation_degrees = p.value("rotation_degrees", 0.0f);
                    r.opacity_decay = p.value("opacity_decay", 1.0f);
                    if (r.copies == 0 || r.copies > 10000 ||
                        !std::isfinite(r.rotation_degrees) || !std::isfinite(r.opacity_decay) ||
                        r.opacity_decay < 0.0f || r.opacity_decay > 1.0f ||
                        !std::isfinite(r.position[0]) || !std::isfinite(r.position[1]) ||
                        !std::isfinite(r.scale[0]) || !std::isfinite(r.scale[1]) ||
                        !std::isfinite(r.anchor[0]) || !std::isfinite(r.anchor[1]))
                        throw std::runtime_error("invalid repeater parameters");
                } else if (kind == "round_corners") {
                    decoded_op.kind = PathOperatorKindPlan::RoundCorners;
                    decoded_op.radius = p.at("radius").get<float>();
                    if (!std::isfinite(decoded_op.radius) || decoded_op.radius < 0.0f)
                        throw std::runtime_error("round-corners radius must be finite and non-negative");
                } else if (kind == "zig_zag") {
                    decoded_op.kind = PathOperatorKindPlan::ZigZag;
                    decoded_op.size = p.at("size").get<float>();
                    decoded_op.points_per_unit = p.value("points_per_unit", 0.1f);
                    decoded_op.smooth = p.value("smooth", false);
                    if (!std::isfinite(decoded_op.size) || decoded_op.size < 0.0f ||
                        !std::isfinite(decoded_op.points_per_unit) ||
                        decoded_op.points_per_unit <= 0.0f || decoded_op.points_per_unit > 1000.0f)
                        throw std::runtime_error("invalid zig-zag parameters");
                } else if (kind == "offset_path") {
                    decoded_op.kind = PathOperatorKindPlan::OffsetPath;
                    decoded_op.amount = p.at("amount").get<float>();
                    decoded_op.miter_limit = p.value("miter_limit", 4.0f);
                    if (!std::isfinite(decoded_op.amount) || !std::isfinite(decoded_op.miter_limit) ||
                        decoded_op.miter_limit < 1.0f || decoded_op.miter_limit > 1000.0f)
                        throw std::runtime_error("invalid offset-path parameters");
                } else if (kind == "pucker_bloat") {
                    decoded_op.kind = PathOperatorKindPlan::PuckerBloat;
                    decoded_op.amount = p.at("amount").get<float>();
                    if (p.contains("anchor")) {
                        const auto& anchor = p.at("anchor");
                        if (!anchor.is_array() || anchor.size() != 2)
                            throw std::runtime_error("pucker-bloat anchor requires two values");
                        decoded_op.anchor = {anchor[0].get<float>(), anchor[1].get<float>()};
                    }
                    if (!std::isfinite(decoded_op.amount) || decoded_op.amount < -1.0f ||
                        decoded_op.amount > 1.0f || !std::isfinite(decoded_op.anchor[0]) ||
                        !std::isfinite(decoded_op.anchor[1]))
                        throw std::runtime_error("invalid pucker-bloat parameters");
                } else if (kind == "twist") {
                    decoded_op.kind = PathOperatorKindPlan::Twist;
                    decoded_op.degrees = p.at("degrees").get<float>();
                    if (p.contains("center")) {
                        const auto& center = p.at("center");
                        if (!center.is_array() || center.size() != 2)
                            throw std::runtime_error("twist center requires two values");
                        decoded_op.center = {center[0].get<float>(), center[1].get<float>()};
                    }
                    if (!std::isfinite(decoded_op.degrees) || !std::isfinite(decoded_op.center[0]) ||
                        !std::isfinite(decoded_op.center[1]))
                        throw std::runtime_error("invalid twist parameters");
                } else if (kind == "wiggle_path") {
                    decoded_op.kind = PathOperatorKindPlan::WigglePath;
                    decoded_op.amount = p.at("amount").get<float>();
                    decoded_op.frequency = p.value("frequency", 1.0f);
                    decoded_op.seed = p.value("seed", 0u);
                    if (!std::isfinite(decoded_op.amount) || decoded_op.amount < 0.0f ||
                        !std::isfinite(decoded_op.frequency) || decoded_op.frequency <= 0.0f ||
                        decoded_op.frequency > 1000.0f)
                        throw std::runtime_error("invalid wiggle-path parameters");
                } else if (kind == "merge") {
                    decoded_op.kind = PathOperatorKindPlan::Merge;
                    const auto& raw_paths = p.at("paths");
                    if (!raw_paths.is_array() || raw_paths.size() > 1024)
                        throw std::runtime_error("merge paths must contain at most 1024 subpaths");
                    std::size_t total_commands = 0;
                    for (const auto& raw_subpath : raw_paths) {
                        if (!raw_subpath.is_array() || raw_subpath.empty() ||
                            raw_subpath.size() > 65536 || total_commands + raw_subpath.size() > 1000000)
                            throw std::runtime_error("merge subpath has invalid command count");
                        std::vector<ShapePathCommandPlan> subpath;
                        for (const auto& raw_command : raw_subpath) {
                            ShapePathCommandPlan command;
                            const auto type = raw_command.at("type").get<std::string>();
                            if (type == "move_to") command.type = ShapePathCommandTypePlan::MoveTo;
                            else if (type == "line_to") command.type = ShapePathCommandTypePlan::LineTo;
                            else if (type == "quadratic_to") command.type = ShapePathCommandTypePlan::QuadraticTo;
                            else if (type == "cubic_to") command.type = ShapePathCommandTypePlan::CubicTo;
                            else if (type == "close") command.type = ShapePathCommandTypePlan::Close;
                            else throw std::runtime_error("unsupported merge path command: " + type);
                            const auto read_point = [](const nlohmann::json& value,
                                                       std::array<float, 2>& out) {
                                if (!value.is_array() || value.size() != 2)
                                    throw std::runtime_error("merge path point must contain two values");
                                out = {value[0].get<float>(), value[1].get<float>()};
                            };
                            if (raw_command.contains("point")) read_point(raw_command.at("point"), command.point);
                            if (raw_command.contains("control1")) read_point(raw_command.at("control1"), command.control1);
                            if (raw_command.contains("control2")) read_point(raw_command.at("control2"), command.control2);
                            const bool has_point = raw_command.contains("point");
                            const bool has_c1 = raw_command.contains("control1");
                            const bool has_c2 = raw_command.contains("control2");
                            if ((command.type == ShapePathCommandTypePlan::MoveTo ||
                                 command.type == ShapePathCommandTypePlan::LineTo) &&
                                (!has_point || has_c1 || has_c2))
                                throw std::runtime_error("merge move_to/line_to require point only");
                            if (command.type == ShapePathCommandTypePlan::QuadraticTo &&
                                (!has_point || !has_c1 || has_c2))
                                throw std::runtime_error("merge quadratic_to requires point and control1");
                            if (command.type == ShapePathCommandTypePlan::CubicTo &&
                                (!has_point || !has_c1 || !has_c2))
                                throw std::runtime_error("merge cubic_to requires point and both controls");
                            if (command.type == ShapePathCommandTypePlan::Close &&
                                (has_point || has_c1 || has_c2))
                                throw std::runtime_error("merge close accepts no coordinates");
                            const auto finite = [](const auto& v) {
                                return std::isfinite(v[0]) && std::isfinite(v[1]);
                            };
                            if ((command.type != ShapePathCommandTypePlan::Close && !finite(command.point)) ||
                                !finite(command.control1) || !finite(command.control2))
                                throw std::runtime_error("merge command coordinates must be finite");
                            subpath.push_back(command);
                        }
                        if (subpath.front().type != ShapePathCommandTypePlan::MoveTo)
                            throw std::runtime_error("each merged subpath must begin with move_to");
                        total_commands += subpath.size();
                        decoded_op.paths.push_back(std::move(subpath));
                    }
                } else {
                    throw std::runtime_error("unsupported path operator: " + kind);
                }
                shape.operators.push_back(decoded_op);
            }
        }
        for (const auto& command : shape.path) {
            const bool needs_point = command.type != ShapePathCommandTypePlan::Close;
            if ((needs_point && !finite_point(command.point)) ||
                !finite_point(command.control1) || !finite_point(command.control2))
                throw std::runtime_error("shape path coordinates must be finite");
        }
        if (raw_shape.contains("fill")) {
            const auto& fill = raw_shape.at("fill");
            if (fill.is_array()) {
                if (fill.size() != 4)
                    throw std::runtime_error("shape.fill must contain RGBA");
                shape.fill_color = std::array<float, 4>{
                    fill[0].get<float>(), fill[1].get<float>(),
                    fill[2].get<float>(), fill[3].get<float>()};
            } else if (fill.is_object()) {
                shape.gradient = decode_gradient_definition(fill);
            }
        }
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
            if (stroke.contains("gradient"))
                decoded_stroke.gradient = decode_gradient_definition(stroke.at("gradient"));
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
        layer.shape = std::move(shape);
    }
    if (value.contains("mask")) {
        const auto& raw_mask = value.at("mask");
        if (raw_mask.value("type", std::string{}) != "path")
            throw std::runtime_error("only path masks are supported by this render-plan extension");
        LayerPlan::PathMaskPlan mask;
        mask.feather = raw_mask.value("feather", 0.0f);
        mask.expansion = raw_mask.value("expansion", 0.0f);
        mask.inverted = raw_mask.value("inverted", false);
        if (!std::isfinite(mask.feather) || mask.feather < 0.0f || mask.feather > 1000000.0f ||
            !std::isfinite(mask.expansion) || std::abs(mask.expansion) > 1000000.0f)
            throw std::runtime_error("path mask feather and expansion must be finite and bounded");
        const auto& raw_path = raw_mask.at("path");
        if (!raw_path.is_array() || raw_path.empty() || raw_path.size() > 65536)
            throw std::runtime_error("path mask requires between 1 and 65536 commands");
        bool has_open_subpath = false;
        std::size_t closed_subpaths = 0;
        for (const auto& raw_command : raw_path) {
            ShapePathCommandPlan command;
            const auto type = raw_command.at("type").get<std::string>();
            if (type == "move_to") command.type = ShapePathCommandTypePlan::MoveTo;
            else if (type == "line_to") command.type = ShapePathCommandTypePlan::LineTo;
            else if (type == "quadratic_to") command.type = ShapePathCommandTypePlan::QuadraticTo;
            else if (type == "cubic_to") command.type = ShapePathCommandTypePlan::CubicTo;
            else if (type == "close") command.type = ShapePathCommandTypePlan::Close;
            else throw std::runtime_error("unsupported path mask command: " + type);
            const auto read_point = [](const nlohmann::json& point, std::array<float, 2>& out) {
                if (!point.is_array() || point.size() != 2)
                    throw std::runtime_error("path mask coordinates require two values");
                out = {point[0].get<float>(), point[1].get<float>()};
            };
            const bool has_point = raw_command.contains("point");
            const bool has_c1 = raw_command.contains("control1");
            const bool has_c2 = raw_command.contains("control2");
            if (has_point) read_point(raw_command.at("point"), command.point);
            if (has_c1) read_point(raw_command.at("control1"), command.control1);
            if (has_c2) read_point(raw_command.at("control2"), command.control2);
            if ((command.type == ShapePathCommandTypePlan::MoveTo ||
                 command.type == ShapePathCommandTypePlan::LineTo) && (!has_point || has_c1 || has_c2))
                throw std::runtime_error("path mask move_to/line_to require point only");
            if (command.type == ShapePathCommandTypePlan::QuadraticTo && (!has_point || !has_c1 || has_c2))
                throw std::runtime_error("path mask quadratic_to requires point and control1");
            if (command.type == ShapePathCommandTypePlan::CubicTo && (!has_point || !has_c1 || !has_c2))
                throw std::runtime_error("path mask cubic_to requires point and both controls");
            if (command.type == ShapePathCommandTypePlan::Close && (has_point || has_c1 || has_c2))
                throw std::runtime_error("path mask close accepts no coordinates");
            for (const auto* pair : {&command.point, &command.control1, &command.control2})
                if (!std::isfinite((*pair)[0]) || !std::isfinite((*pair)[1]))
                    throw std::runtime_error("path mask coordinates must be finite");
            if (command.type == ShapePathCommandTypePlan::MoveTo) {
                if (has_open_subpath) throw std::runtime_error("path mask subpaths must close before move_to");
                has_open_subpath = true;
            }
            if (command.type == ShapePathCommandTypePlan::Close) {
                if (!has_open_subpath) throw std::runtime_error("path mask close has no open subpath");
                has_open_subpath = false;
                ++closed_subpaths;
            }
            mask.path.push_back(command);
        }
        if (has_open_subpath || closed_subpaths == 0)
            throw std::runtime_error("path mask subpaths must be closed");
        layer.path_mask = std::move(mask);
    }
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t i = 0; i < position.size() && i < layer.position.size(); ++i) {
            layer.position[i] = position.at(i).get<float>();
            if (!std::isfinite(layer.position[i]))
                throw std::runtime_error("layer.position components must be finite");
        }
    }

    if (value.contains("scale")) {
        const auto& scale = value.at("scale");
        layer.scale_dimensions = scale.size();
        for (std::size_t i = 0; i < scale.size() && i < layer.scale.size(); ++i)
            layer.scale[i] = scale.at(i).get<float>();
    }
    if (value.contains("rotation")) {
        const auto& rotation = value.at("rotation");
        layer.rotation_dimensions = rotation.size();
        for (std::size_t i = 0; i < rotation.size() && i < layer.rotation.size(); ++i)
            layer.rotation[i] = rotation.at(i).get<float>();
    }
    layer.enable_3d = value.value("enable_3d", false);
    layer.screen_space = value.value("screen_space", false);
    if (value.contains("depth_role")) {
        layer.depth_role = value.at("depth_role").get<std::string>();
        static constexpr std::array<std::string_view, 7> kRoles{
            "far_background", "background", "midground", "subject",
            "atmosphere", "foreground", "overlay"};
        if (std::find(kRoles.begin(), kRoles.end(), *layer.depth_role) == kRoles.end())
            throw std::runtime_error(
                "unsupported depth_role '" + *layer.depth_role +
                "'; valid roles: far_background, background, midground, subject, atmosphere, foreground, overlay");
    }
    if (value.contains("depth_offset"))
        layer.depth_offset = value.at("depth_offset").get<float>();

    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    layer.radius = optional_value<float>(value, "radius");

    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font = style.value("font", std::string{});
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{stroke.value("color", std::string{}),
                                            optional_value<float>(stroke, "width"),
                                            std::nullopt, std::nullopt, std::nullopt};
        }
        if (style.contains("shadow")) {
            const auto& shadow = style.at("shadow");
            ShadowStyle shadow_plan;
            shadow_plan.color = shadow.value("color", std::string{});
            shadow_plan.opacity = optional_value<float>(shadow, "opacity");
            shadow_plan.blur = optional_value<float>(shadow, "blur");
            if (shadow.contains("offset")) {
                const auto& offset = shadow.at("offset");
                shadow_plan.offset_dimensions = offset.size();
                for (std::size_t i = 0; i < offset.size() && i < shadow_plan.offset.size(); ++i)
                    shadow_plan.offset[i] = offset.at(i).get<float>();
            }
            style_plan.shadow = std::move(shadow_plan);
        }
        if (style.contains("glow")) {
            const auto& glow = style.at("glow");
            GlowStyle glow_plan;
            if (glow.contains("radius"))
                glow_plan.radius = glow.at("radius").get<float>();
            if (glow.contains("intensity"))
                glow_plan.intensity = glow.at("intensity").get<float>();
            if (glow.contains("color")) {
                glow_plan.color = glow.at("color").get<std::string>();
            } else {
                // Legacy plans declared the glow tint implicitly as white.
                // Old lobe and quality knobs are accepted but ignored.
                glow_plan.color = "#FFFFFF";
            }
            if (!std::isfinite(glow_plan.radius) || glow_plan.radius < 0.0f ||
                !std::isfinite(glow_plan.intensity) || glow_plan.intensity < 0.0f) {
                throw std::runtime_error(
                    "style.glow radius and intensity must be finite and non-negative");
            }
            style_plan.glow = std::move(glow_plan);
        }
        if (style.contains("background")) {
            const auto& background = style.at("background");
            BackgroundStyle background_plan;
            background_plan.color = background.value("color", std::string{});
            background_plan.opacity = optional_value<float>(background, "opacity");
            background_plan.radius = optional_value<float>(background, "radius");
            if (background.contains("padding")) {
                const auto& padding = background.at("padding");
                background_plan.padding_dimensions = padding.size();
                for (std::size_t i = 0; i < padding.size() && i < background_plan.padding.size(); ++i)
                    background_plan.padding[i] = padding.at(i).get<float>();
            }
            style_plan.background = std::move(background_plan);
        }
        layer.font = style_plan.font;
        layer.style = std::move(style_plan);
    }

    if (value.contains("animation")) {
        AnimationPlan animation;
        for (const auto& track_value : value.at("animation").at("tracks"))
            animation.tracks.push_back(decode_animation_track(track_value));
        layer.animation = std::move(animation);
    }
    if (value.contains("text_animators")) {
        for (const auto& animator : value.at("text_animators"))
            layer.text_animators.push_back(decode_text_animator(animator));
    }
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>()))
            layer.blend_mode = mode;
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    return layer;
}

}  // namespace chronon3d::render_plan::detail
