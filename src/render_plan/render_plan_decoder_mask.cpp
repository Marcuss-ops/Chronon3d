#include "render_plan_decoder_detail.hpp"
#include <chronon3d/scene/model/shape/path_operator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {
std::vector<ShapePathCommandPlan> decode_closed_mask_path(const nlohmann::json& raw_path) {
    if (!raw_path.is_array() || raw_path.empty() || raw_path.size() > 65536)
        throw std::runtime_error("path mask requires between 1 and 65536 commands");
    std::vector<ShapePathCommandPlan> path;
    path.reserve(raw_path.size());
    bool open = false;
    std::size_t closed = 0;
    for (const auto& raw : raw_path) {
        auto command = decode_shape_path_command(raw, "path mask");
        for (const auto* coordinates : {&command.point, &command.control1, &command.control2})
            if (std::abs((*coordinates)[0]) > 1000000.0f ||
                std::abs((*coordinates)[1]) > 1000000.0f)
                throw std::runtime_error("path mask coordinates must be within [-1000000, 1000000]");
        if (command.type == ShapePathCommandTypePlan::MoveTo) {
            if (open) throw std::runtime_error("path mask subpaths must close before move_to");
            open = true;
        }
        if (command.type == ShapePathCommandTypePlan::Close) {
            if (!open) throw std::runtime_error("path mask close has no open subpath");
            open = false;
            ++closed;
        }
        path.push_back(command);
    }
    if (open || closed == 0)
        throw std::runtime_error("path mask subpaths must be closed");
    return path;
}

std::vector<PathCommand> mask_path_commands(const std::vector<ShapePathCommandPlan>& path) {
    std::vector<PathCommand> commands;
    commands.reserve(path.size());
    for (const auto& command : path) {
        const Vec2 point{command.point[0], command.point[1]};
        const Vec2 c1{command.control1[0], command.control1[1]};
        const Vec2 c2{command.control2[0], command.control2[1]};
        switch (command.type) {
            case ShapePathCommandTypePlan::MoveTo: commands.push_back(PathCommand::move_to(point)); break;
            case ShapePathCommandTypePlan::LineTo: commands.push_back(PathCommand::line_to(point)); break;
            case ShapePathCommandTypePlan::QuadraticTo: commands.push_back(PathCommand::quadratic_to(c1, point)); break;
            case ShapePathCommandTypePlan::CubicTo: commands.push_back(PathCommand::cubic_to(c1, c2, point)); break;
            case ShapePathCommandTypePlan::Close: commands.push_back(PathCommand::close()); break;
        }
    }
    return commands;
}

std::optional<AnimationTrackPlan> decode_mask_parameter_track(
    const nlohmann::json& raw, const char* field, std::string_view property,
    float minimum, float maximum) {
    if (!raw.contains(field)) return std::nullopt;
    auto track = decode_animation_track(raw.at(field));
    if (!track.property.empty() && track.property != property)
        throw std::runtime_error(std::string("mask ") + field +
                                 " property must be '" + std::string(property) + "'");
    track.property = property;
    if (track.keyframes.empty())
        throw std::runtime_error(std::string("mask ") + field + " requires keyframes");
    for (const auto& key : track.keyframes)
        if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
            key.value.front() < minimum || key.value.front() > maximum)
            throw std::runtime_error(std::string("mask ") + field +
                                     " keyframes are outside supported bounds");
    return track;
}

LayerPlan::PathMaskPlan decode_mask_plan(const nlohmann::json& raw) {
    if (!raw.is_object()) throw std::runtime_error("mask stack entries must be objects");
    LayerPlan::PathMaskPlan mask;
    mask.type = raw.at("type").get<std::string>();
    mask.mode = raw.at("mode").get<std::string>();
    if (mask.type != "path" && mask.type != "rect" &&
        mask.type != "rounded_rect" && mask.type != "ellipse" &&
        mask.type != "field" && mask.type != "text")
        throw std::runtime_error("unsupported mask geometry: " + mask.type);
    if (mask.mode != "add" && mask.mode != "subtract" &&
        mask.mode != "intersect" && mask.mode != "difference")
        throw std::runtime_error("unsupported mask combination mode: " + mask.mode);
    const auto read_vec2 = [&](const char* key, std::array<float, 2>& output,
                               bool strictly_positive) {
        if (!raw.contains(key)) return;
        const auto& value = raw.at(key);
        if (!value.is_array() || value.size() != 2)
            throw std::runtime_error(std::string("mask ") + key + " requires [x, y]");
        output = {value[0].get<float>(), value[1].get<float>()};
        for (const float component : output)
            if (!std::isfinite(component) || (strictly_positive ? component <= 0.0f : std::abs(component) > 1000000.0f))
                throw std::runtime_error(std::string("mask ") + key + " values are outside supported bounds");
        if (strictly_positive && (output[0] > 1000000.0f || output[1] > 1000000.0f))
            throw std::runtime_error(std::string("mask ") + key + " values exceed supported bounds");
    };
    read_vec2("position", mask.position, false);
    read_vec2("size", mask.size, true);
    mask.radius = raw.value("radius", 0.0f);
    mask.feather = raw.value("feather", 0.0f);
    mask.expansion = raw.value("expansion", 0.0f);
    mask.opacity = raw.value("opacity", 1.0f);
    mask.feather_track = decode_mask_parameter_track(raw, "feather_track", "mask_feather", 0.0f, 1000000.0f);
    mask.expansion_track = decode_mask_parameter_track(raw, "expansion_track", "mask_expansion", -1000000.0f, 1000000.0f);
    mask.opacity_track = decode_mask_parameter_track(raw, "opacity_track", "mask_opacity", 0.0f, 1.0f);
    mask.inverted = raw.value("inverted", false);
    if (!std::isfinite(mask.radius) || mask.radius < 0.0f || mask.radius > 500000.0f ||
        !std::isfinite(mask.feather) || mask.feather < 0.0f || mask.feather > 1000000.0f ||
        !std::isfinite(mask.expansion) || std::abs(mask.expansion) > 1000000.0f ||
        !std::isfinite(mask.opacity) || mask.opacity < 0.0f || mask.opacity > 1.0f)
        throw std::runtime_error("mask radius, feather, and expansion must be finite and bounded");
    if (mask.type == "text") {
        mask.text_source = raw.at("source").get<std::string>();
        if (mask.text_source.empty() || mask.text_source.size() > 256)
            throw std::runtime_error("text mask source must be a non-empty layer id");
        if (raw.contains("path") || raw.contains("target_path") || raw.contains("animation") ||
            raw.contains("field") || raw.contains("size") || raw.contains("radius"))
            throw std::runtime_error("text masks accept a source layer, not geometric parameters");
    } else if (mask.type == "path") {
        if (raw.contains("field"))
            throw std::runtime_error("field sampler is only valid for field masks");
        if (raw.contains("size") || raw.contains("radius"))
            throw std::runtime_error("path mask geometry derives its bounds from path commands");
        mask.path = decode_closed_mask_path(raw.at("path"));
        if (raw.contains("target_path")) {
            mask.morph_target = decode_closed_mask_path(raw.at("target_path"));
            const auto source = mask_path_commands(mask.path);
            const auto target = mask_path_commands(mask.morph_target);
            const auto topology_error = validate_morph_path_topology(source, target);
            if (topology_error)
                throw std::runtime_error("mask target_path topology must match path");
            if (!raw.contains("animation"))
                throw std::runtime_error("mask target_path requires animation");
            auto animation = decode_animation_track(raw.at("animation"));
            animation.property = "mask_morph";
            if (animation.keyframes.empty())
                throw std::runtime_error("mask path animation requires keyframes");
            for (const auto& key : animation.keyframes)
                if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                    key.value.front() < 0.0f || key.value.front() > 1.0f)
                    throw std::runtime_error("mask path animation requires scalar progress in [0,1]");
            mask.morph_animation = std::move(animation);
        } else if (raw.contains("animation")) {
            throw std::runtime_error("mask path animation requires target_path");
        }
    } else {
        if (raw.contains("path") || raw.contains("target_path") || raw.contains("animation"))
            throw std::runtime_error("only path masks accept path commands or morph animation");
        if (mask.type == "field")
            mask.field = decode_field2d_plan(raw.at("field"));
        else if (raw.contains("field"))
            throw std::runtime_error("field is only valid for field masks");
        if (mask.type == "rounded_rect" && mask.radius > std::min(mask.size[0], mask.size[1]) * 0.5f)
            throw std::runtime_error("rounded_rect mask radius cannot exceed half its smaller dimension");
        if (mask.type != "rounded_rect" && raw.contains("radius"))
            throw std::runtime_error("radius is only valid for rounded_rect masks");
    }
    return mask;
}


}  // namespace

void decode_layer_masks(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("mask") && value.contains("masks"))
        throw std::runtime_error("use either legacy mask or ordered masks, not both");
    if (value.contains("mask")) {
        const auto& raw_mask = value.at("mask");
        if (raw_mask.value("type", std::string{}) != "path")
            throw std::runtime_error("only path masks are supported by this render-plan extension");
        LayerPlan::PathMaskPlan mask;
        mask.feather = raw_mask.value("feather", 0.0f);
        mask.expansion = raw_mask.value("expansion", 0.0f);
        mask.opacity = raw_mask.value("opacity", 1.0f);
        mask.feather_track = decode_mask_parameter_track(raw_mask, "feather_track", "mask_feather", 0.0f, 1000000.0f);
        mask.expansion_track = decode_mask_parameter_track(raw_mask, "expansion_track", "mask_expansion", -1000000.0f, 1000000.0f);
        mask.opacity_track = decode_mask_parameter_track(raw_mask, "opacity_track", "mask_opacity", 0.0f, 1.0f);
        mask.inverted = raw_mask.value("inverted", false);
        if (!std::isfinite(mask.feather) || mask.feather < 0.0f || mask.feather > 1000000.0f ||
            !std::isfinite(mask.expansion) || std::abs(mask.expansion) > 1000000.0f ||
            !std::isfinite(mask.opacity) || mask.opacity < 0.0f || mask.opacity > 1.0f)
            throw std::runtime_error("path mask feather and expansion must be finite and bounded");
        mask.path = decode_closed_mask_path(raw_mask.at("path"));
        if (raw_mask.contains("target_path")) {
            mask.morph_target = decode_closed_mask_path(raw_mask.at("target_path"));
            if (const auto error = validate_morph_path_topology(
                    [&] {
                        std::vector<PathCommand> source;
                        source.reserve(mask.path.size());
                        for (const auto& command : mask.path) {
                            const Vec2 point{command.point[0], command.point[1]};
                            const Vec2 c1{command.control1[0], command.control1[1]};
                            const Vec2 c2{command.control2[0], command.control2[1]};
                            switch (command.type) {
                                case ShapePathCommandTypePlan::MoveTo: source.push_back(PathCommand::move_to(point)); break;
                                case ShapePathCommandTypePlan::LineTo: source.push_back(PathCommand::line_to(point)); break;
                                case ShapePathCommandTypePlan::QuadraticTo: source.push_back(PathCommand::quadratic_to(c1, point)); break;
                                case ShapePathCommandTypePlan::CubicTo: source.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                case ShapePathCommandTypePlan::Close: source.push_back(PathCommand::close()); break;
                            }
                        }
                        return source;
                    }(),
                    [&] {
                        std::vector<PathCommand> target;
                        target.reserve(mask.morph_target.size());
                        for (const auto& command : mask.morph_target) {
                            const Vec2 point{command.point[0], command.point[1]};
                            const Vec2 c1{command.control1[0], command.control1[1]};
                            const Vec2 c2{command.control2[0], command.control2[1]};
                            switch (command.type) {
                                case ShapePathCommandTypePlan::MoveTo: target.push_back(PathCommand::move_to(point)); break;
                                case ShapePathCommandTypePlan::LineTo: target.push_back(PathCommand::line_to(point)); break;
                                case ShapePathCommandTypePlan::QuadraticTo: target.push_back(PathCommand::quadratic_to(c1, point)); break;
                                case ShapePathCommandTypePlan::CubicTo: target.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                case ShapePathCommandTypePlan::Close: target.push_back(PathCommand::close()); break;
                            }
                        }
                        return target;
                    }()))
                throw std::runtime_error("path mask target_path topology must match path");
            if (!raw_mask.contains("animation"))
                throw std::runtime_error("path mask target_path requires an animation track");
            auto animation = decode_animation_track(raw_mask.at("animation"));
            animation.property = "mask_morph";
            for (const auto& key : animation.keyframes)
                if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                    key.value.front() < 0.0f || key.value.front() > 1.0f)
                    throw std::runtime_error("path mask animation requires scalar progress in [0,1]");
            if (animation.keyframes.empty())
                throw std::runtime_error("path mask animation requires keyframes");
            mask.morph_animation = std::move(animation);
        } else if (raw_mask.contains("animation")) {
            throw std::runtime_error("path mask animation requires target_path");
        }
        layer.path_mask = std::move(mask);
    }
    if (value.contains("masks")) {
        const auto& raw_masks = value.at("masks");
        if (!raw_masks.is_array() || raw_masks.empty() || raw_masks.size() > 16)
            throw std::runtime_error("masks must contain between 1 and 16 ordered entries");
        layer.masks.reserve(raw_masks.size());
        std::size_t total_mask_commands = 0;
        for (const auto& raw_mask : raw_masks) {
            layer.masks.push_back(decode_mask_plan(raw_mask));
            total_mask_commands += layer.masks.back().path.size() + layer.masks.back().morph_target.size();
            if (total_mask_commands > 65536)
                throw std::runtime_error("mask stack total path command limit is 65536");
        }
        if (layer.masks.front().mode != "add")
            throw std::runtime_error("the first mask stack entry must use mode add");
    }
}

}  // namespace chronon3d::render_plan::detail
