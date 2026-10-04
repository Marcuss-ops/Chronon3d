#include "render_plan_compiler_lottie_detail.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>
namespace chronon3d::render_plan::detail {
namespace { using json=nlohmann::json; }
bool lottie_identity_transform(const json& transform) {
    std::array<float, 2> position{};
    std::array<float, 2> anchor{};
    std::array<float, 2> scale{};
    float rotation = 0.0f;
    float opacity = 0.0f;
    return lottie_static_vec2_property(transform, "p", position) &&
           lottie_static_vec2_property(transform, "a", anchor) &&
           lottie_static_vec2_property(transform, "s", scale) &&
           lottie_static_scalar_property(transform, "r", rotation) &&
           lottie_static_scalar_property(transform, "o", opacity) &&
           position[0] == 0.0f && position[1] == 0.0f &&
           anchor[0] == 0.0f && anchor[1] == 0.0f &&
           scale[0] == 100.0f && scale[1] == 100.0f &&
           rotation == 0.0f && opacity == 100.0f;
}
bool lottie_static_vec2_property(const json& owner, const char* key,
                                              std::array<float, 2>& out) {

    if (!owner.contains(key) || !owner[key].is_object() ||
        owner[key].value("a", 1) != 0 || !owner[key].contains("k")) return false;
    const auto& value = owner[key]["k"];
    if (!value.is_array() || value.size() != 2) return false;
    for (std::size_t i = 0; i < 2; ++i) {
        if (!value[i].is_number()) return false;
        out[i] = value[i].get<float>();
        if (!std::isfinite(out[i]) || std::abs(out[i]) > 100000.0f) return false;
    }
    return true;

}

bool lottie_static_scalar_property(const json& owner, const char* key,
                                                float& out) {

    if (!owner.contains(key) || !owner[key].is_object() ||
        owner[key].value("a", 1) != 0 || !owner[key].contains("k") ||
        !owner[key]["k"].is_number()) return false;
    out = owner[key]["k"].get<float>();
    return std::isfinite(out) && std::abs(out) <= 100000.0f;

}

bool lottie_read_transform_property(const json& transform, const char* name,
                                                std::vector<float>& result, bool& animated) {

    if (!transform.contains(name) || !transform[name].is_object() ||
        !transform[name].contains("k")) return false;
    const auto& property = transform[name];
    animated = property.value("a", 0) == 1;
    if (animated) return true;
    const auto& value = property["k"];
    result.clear();
    if (value.is_number()) result.push_back(value.get<float>());
    else if (value.is_array()) {
        for (const auto& component : value) {
            if (!component.is_number()) return false;
            result.push_back(component.get<float>());
        }
    } else return false;
    return !result.empty() && std::all_of(result.begin(), result.end(), [](float v) {
        return std::isfinite(v) && std::abs(v) <= 100000.0f;
    });

}

bool lottie_decode_transform_track(const json& transform, const char* name,
                                   const char* property, float divisor,
                                   AnimationTrackPlan& track) {

    if (!transform.contains(name) || !transform[name].is_object() ||
        transform[name].value("a", 0) != 1 || !transform[name].contains("k") ||
        !transform[name]["k"].is_array() || transform[name]["k"].empty()) return false;
    const auto& raw_keys = transform[name]["k"];
    track.property = property;
    const auto components = [](const json& raw, std::vector<float>& result) {
        result.clear();
        const json* value = &raw;
        if (raw.is_array() && raw.size() == 1 && raw[0].is_array()) value = &raw[0];
        if (value->is_number()) result.push_back(value->get<float>());
        else if (value->is_array()) {
            for (const auto& component : *value) {
                if (!component.is_number()) return false;
                result.push_back(component.get<float>());
            }
        } else return false;
        return !result.empty() && std::all_of(result.begin(), result.end(), [](float v) {
            return std::isfinite(v) && std::abs(v) <= 100000.0f;
        });
    };
    for (std::size_t index = 0; index < raw_keys.size(); ++index) {
        const auto& raw = raw_keys[index];
        if (!raw.is_object() || !raw.contains("t") || !raw["t"].is_number() ||
            !raw.contains("s")) return false;
        const double time = raw["t"].get<double>();
        std::vector<float> value;
        if (!std::isfinite(time) || time < 0.0 || time > 1000000.0 ||
            std::floor(time) != time || !components(raw["s"], value)) return false;
        for (auto& component : value) component /= divisor;
        AnimationKeyframePlan key;
        key.frame = Frame{static_cast<std::int64_t>(time)};
        key.value = std::move(value);
        if (index + 1 < raw_keys.size()) {
            if (raw.value("h", 0) == 1) key.interpolation = "hold";
            else {
                key.interpolation = "linear";
                const auto& next = raw_keys[index + 1];
                if (raw.contains("o") && next.is_object() && next.contains("i")) {
                    const auto read_handle = [](const json& raw_handle,
                                                std::array<float, 2>& handle) {
                        if (!raw_handle.is_object() || !raw_handle.contains("x") ||
                            !raw_handle.contains("y")) return false;
                        const auto first = [](const json& v, float& out) {
                            const json* item = &v;
                            if (v.is_array() && !v.empty()) item = &v[0];
                            if (!item->is_number()) return false;
                            out = item->get<float>();
                            return std::isfinite(out);
                        };
                        return first(raw_handle["x"], handle[0]) &&
                               first(raw_handle["y"], handle[1]);
                    };
                    std::array<float, 2> outgoing{}, incoming{};
                    if (!read_handle(raw["o"], outgoing) || !read_handle(next["i"], incoming))
                        return false;
                    key.interpolation = "bezier";
                    key.bezier_curve = std::array<float, 4>{
                        std::clamp(outgoing[0], 0.0f, 1.0f),
                        std::clamp(outgoing[1], -10.0f, 10.0f),
                        std::clamp(incoming[0], 0.0f, 1.0f),
                        std::clamp(incoming[1], -10.0f, 10.0f)};
                }
            }
        }
        track.keyframes.push_back(std::move(key));
    }
    if (track.keyframes.size() < 2)
        return false;
    const auto dimensions = track.keyframes.front().value.size();
    const auto property_name = std::string_view(property);
    if ((property_name == "position" && (dimensions < 2 || dimensions > 3)) ||
        (property_name == "scale" && (dimensions < 2 || dimensions > 3)) ||
        ((property_name == "rotation_z" || property_name == "opacity") && dimensions != 1))
        return false;
    for (std::size_t index = 1; index < track.keyframes.size(); ++index) {
        if (track.keyframes[index].frame <= track.keyframes[index - 1].frame ||
            track.keyframes[index].value.size() != dimensions) return false;
    }
    return true;

}
std::optional<std::string> decode_lottie_layer_transform(
    LayerPlan& layer_plan, const json& layer, float& anchor_x, float& anchor_y) {
if (!layer.contains("ks") || !layer["ks"].is_object())
    return std::string("Lottie layer transform is missing");
auto& transform = layer["ks"];
std::vector<float> position, anchor, scale, rotation, opacity;
bool p_animated{}, a_animated{}, s_animated{}, r_animated{}, o_animated{};
if (!lottie_read_transform_property(transform, "p", position, p_animated) ||
    !lottie_read_transform_property(transform, "a", anchor, a_animated) ||
    !lottie_read_transform_property(transform, "s", scale, s_animated) ||
    !lottie_read_transform_property(transform, "r", rotation, r_animated) ||
    !lottie_read_transform_property(transform, "o", opacity, o_animated))
    return std::string("Lottie transform properties are malformed or unsupported");
if ((!p_animated && (position.size() < 2 || position.size() > 3)) ||
    (!a_animated && (anchor.size() < 2 || anchor.size() > 3)) ||
    (!s_animated && (scale.size() < 2 || scale.size() > 3)) ||
    (!r_animated && rotation.size() != 1) ||
    (!o_animated && opacity.size() != 1))
    return std::string("Lottie transform property dimensions are unsupported");
if (p_animated || a_animated || s_animated || r_animated || o_animated) {
    if (!layer_plan.animation) layer_plan.animation.emplace();
    auto add_track = [&](const char* name, const char* property, float divisor) {
        AnimationTrackPlan track;
        if (!lottie_decode_transform_track(transform, name, property, divisor, track)) return false;
        layer_plan.animation->tracks.push_back(std::move(track));
        return true;
    };
    if (p_animated && !add_track("p", "position", 1.0f))
        return std::string("unsupported Lottie position keyframes");
    if (s_animated && !add_track("s", "scale", 100.0f))
        return std::string("unsupported Lottie scale keyframes");
    if (r_animated && !add_track("r", "rotation_z", 1.0f))
        return std::string("unsupported Lottie rotation keyframes");
    if (o_animated && !add_track("o", "opacity", 100.0f))
        return std::string("unsupported Lottie opacity keyframes");
    if (a_animated)
        return std::string("animated Lottie anchor is not representable by the layer transform");
}
const auto component_or = [](const std::vector<float>& values, std::size_t index, float fallback) {
    return index < values.size() ? values[index] : fallback;
};
if (!p_animated) {
    layer_plan.position = {component_or(position, 0, 0),
                           component_or(position, 1, 0),
                           component_or(position, 2, 0)};
    layer_plan.position_dimensions = position.size();
}
if (!s_animated) {
    layer_plan.scale = {component_or(scale, 0, 100) / 100.0f,
                        component_or(scale, 1, 100) / 100.0f,
                        component_or(scale, 2, 100) / 100.0f};
    layer_plan.scale_dimensions = scale.size();
}
if (!r_animated) {
    layer_plan.rotation[2] = component_or(rotation, 0, 0);
    layer_plan.rotation_dimensions = 1;
}
if (!o_animated) {
    if (component_or(opacity, 0, 100) < 0.0f || component_or(opacity, 0, 100) > 100.0f)
        return std::string("Lottie opacity must be within [0, 100]");
    layer_plan.opacity = component_or(opacity, 0, 100) / 100.0f;
}
if (a_animated)
    return std::string("animated Lottie anchor is not representable by the layer transform");
anchor_x = component_or(anchor, 0, 0);
anchor_y = component_or(anchor, 1, 0);
return std::nullopt;
}
}
