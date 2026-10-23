#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/shape_path_generator.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

void decode_shape_path_operators(ShapeContentPlan& shape,
                                 const nlohmann::json& raw_shape) {
    const auto finite_point = [](const std::array<float, 2>& point) {
        return std::isfinite(point[0]) && std::isfinite(point[1]);
    };
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
            if (raw_op.contains("target")) {
                decoded_op.target_id = raw_op.at("target").get<std::string>();
                if (decoded_op.target_id.empty() || decoded_op.target_id.size() > 128)
                    throw std::runtime_error("path operator target must be a non-empty SVG id up to 128 characters");
                if (!shape.svg_asset)
                    throw std::runtime_error("path operator target is supported only for imported SVG assets");
            }
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
                if (p.contains("animation")) {
                    auto animation = decode_animation_track(p.at("animation"));
                    animation.property = "repeater";
                    for (const auto& key : animation.keyframes) {
                        if (key.value.empty() || key.value.size() > 5 ||
                            std::any_of(key.value.begin(), key.value.end(), [](float value) {
                                return !std::isfinite(value) || std::abs(value) > 1000000.0f;
                            }))
                            throw std::runtime_error(
                                "repeater animation keyframes require 1..5 bounded finite values [position_x, position_y, scale_x, scale_y, rotation_degrees]");
                    }
                    decoded_op.repeater_animation = std::move(animation);
                }
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
                    std::abs(decoded_op.amount) > 1.0e10f ||
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
            } else if (kind == "merge" || kind == "boolean_union") {
                decoded_op.kind = kind == "merge" ? PathOperatorKindPlan::Merge
                                                    : PathOperatorKindPlan::BooleanUnion;
                if (shape.svg_asset && decoded_op.target_id.empty())
                    throw std::runtime_error(
                        "SVG merge/boolean-union requires an exact element target id");
                const auto& raw_paths = p.at("paths");
                if (!raw_paths.is_array() || raw_paths.size() > 1024)
                        throw std::runtime_error("path operation accepts at most 1024 subpaths");
                std::size_t total_commands = 0;
                for (const auto& raw_subpath : raw_paths) {
                    if (!raw_subpath.is_array() || raw_subpath.empty() ||
                        raw_subpath.size() > 65536 || total_commands + raw_subpath.size() > 1000000)
                        throw std::runtime_error("path operation subpath has invalid command count");
                    std::vector<ShapePathCommandPlan> subpath;
                    for (const auto& raw_command : raw_subpath) {
                        auto command = decode_shape_path_command(raw_command, "path operation");
                        const auto finite = [](const auto& v) {
                            return std::isfinite(v[0]) && std::isfinite(v[1]);
                        };
                        if ((command.type != ShapePathCommandTypePlan::Close && !finite(command.point)) ||
                            !finite(command.control1) || !finite(command.control2))
                            throw std::runtime_error("path operation coordinates must be finite");
                        subpath.push_back(command);
                    }
                    if (subpath.front().type != ShapePathCommandTypePlan::MoveTo)
                        throw std::runtime_error("each path-operation subpath must begin with move_to");
                    total_commands += subpath.size();
                    decoded_op.paths.push_back(std::move(subpath));
                }
            } else if (kind == "morph") {
                decoded_op.kind = PathOperatorKindPlan::Morph;
                if (!p.contains("target"))
                    throw std::runtime_error("morph operator requires target commands");
                const auto& raw_target = p.at("target");
                if (!raw_target.is_array() || raw_target.empty() || raw_target.size() > 65536)
                    throw std::runtime_error("morph target must contain between 1 and 65536 commands");
                for (const auto& raw_command : raw_target) {
                    auto command = decode_shape_path_command(raw_command, "morph path");
                    if ((command.type != ShapePathCommandTypePlan::Close &&
                         !finite_point(command.point)) ||
                        !finite_point(command.control1) || !finite_point(command.control2))
                        throw std::runtime_error("morph path coordinates must be finite");
                    decoded_op.morph_target.push_back(command);
                }
                std::vector<PathCommand> source_commands;
                source_commands.reserve(shape.path.size());
                const auto make_path_command = [](const ShapePathCommandPlan& command) {
                    const Vec2 point{command.point[0], command.point[1]};
                    const Vec2 control1{command.control1[0], command.control1[1]};
                    const Vec2 control2{command.control2[0], command.control2[1]};
                    switch (command.type) {
                        case ShapePathCommandTypePlan::MoveTo: return PathCommand::move_to(point);
                        case ShapePathCommandTypePlan::LineTo: return PathCommand::line_to(point);
                        case ShapePathCommandTypePlan::QuadraticTo: return PathCommand::quadratic_to(control1, point);
                        case ShapePathCommandTypePlan::CubicTo: return PathCommand::cubic_to(control1, control2, point);
                        case ShapePathCommandTypePlan::Close: return PathCommand::close();
                    }
                    return PathCommand::close();
                };
                for (const auto& command : shape.path)
                    source_commands.push_back(make_path_command(command));
                std::vector<PathCommand> target_commands;
                target_commands.reserve(decoded_op.morph_target.size());
                for (const auto& command : decoded_op.morph_target)
                    target_commands.push_back(make_path_command(command));
                if (shape.svg_asset) {
                    if (decoded_op.target_id.empty())
                        throw std::runtime_error(
                            "SVG morph operators require an exact element target id");
                    // SVG source geometry is resolved by AssetResolver during
                    // compilation, so validate topology against that element there.
                } else if (const auto topology_error = validate_morph_path_topology(
                               source_commands, target_commands)) {
                    throw std::runtime_error(*topology_error);
                }
                decoded_op.morph_progress = p.value("progress", 0.0f);
                if (!std::isfinite(decoded_op.morph_progress) ||
                    decoded_op.morph_progress < 0.0f || decoded_op.morph_progress > 1.0f)
                    throw std::runtime_error("morph progress must be finite and in [0, 1]");
                if (p.contains("animation")) {
                    auto animation = decode_animation_track(p.at("animation"));
                    animation.property = "morph_progress";
                    for (const auto& key : animation.keyframes) {
                        if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                            key.value.front() < 0.0f || key.value.front() > 1.0f)
                            throw std::runtime_error("morph animation keyframes require scalar progress in [0, 1]");
                    }
                    decoded_op.morph_animation = std::move(animation);
                }
            } else if (kind == "trim") {
                decoded_op.kind = PathOperatorKindPlan::Trim;
                decoded_op.trim_start = p.value("start", 0.0f);
                decoded_op.trim_end = p.value("end", 1.0f);
                decoded_op.trim_offset = p.value("offset", 0.0f);
                if (!std::isfinite(decoded_op.trim_start) || !std::isfinite(decoded_op.trim_end) ||
                    !std::isfinite(decoded_op.trim_offset) ||
                    decoded_op.trim_start < 0.0f || decoded_op.trim_start > 1.0f ||
                    decoded_op.trim_end < 0.0f || decoded_op.trim_end > 1.0f ||
                    decoded_op.trim_end < decoded_op.trim_start ||
                    std::abs(decoded_op.trim_offset) > 1000000.0f)
                    throw std::runtime_error("invalid trim-path parameters");
                if (p.contains("animation")) {
                    auto animation = decode_animation_track(p.at("animation"));
                    animation.property = "trim";
                    for (const auto& key : animation.keyframes) {
                        if (key.value.empty() || key.value.size() > 3 ||
                            (key.value.size() > 0 &&
                             (!std::isfinite(key.value[0]) || key.value[0] < 0.0f || key.value[0] > 1.0f)) ||
                            (key.value.size() > 1 &&
                             (!std::isfinite(key.value[1]) || key.value[1] < 0.0f || key.value[1] > 1.0f)) ||
                            (key.value.size() > 2 && !std::isfinite(key.value[2])))
                            throw std::runtime_error("trim animation keyframes require 1..3 finite values [start, end, offset]");
                    }
                    decoded_op.trim_animation = std::move(animation);
                }
            } else {
                throw std::runtime_error("unsupported path operator: " + kind);
            }
            shape.operators.push_back(decoded_op);
        }
    }
}

}  // namespace chronon3d::render_plan::detail
