#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {

using json = nlohmann::json;

bool lottie_static_effect_value(const json& effect, std::size_t index,
                                std::vector<float>& values) {
    if (!effect.contains("ef") || !effect["ef"].is_array() ||
        index >= effect["ef"].size()) return false;
    const auto& parameter = effect["ef"][index];
    if (!parameter.is_object() || !parameter.contains("v") || !parameter["v"].is_object())
        return false;
    const auto& property = parameter["v"];
    if (property.value("a", 0) != 0 || !property.contains("k")) return false;
    const auto& key = property["k"];
    values.clear();
    if (key.is_number()) values.push_back(key.get<float>());
    else if (key.is_array()) {
        for (const auto& component : key) {
            if (!component.is_number()) return false;
            values.push_back(component.get<float>());
        }
    } else return false;
    return std::all_of(values.begin(), values.end(),
        [](float value) { return std::isfinite(value); });
}

std::optional<std::string> decode_lottie_layer_effects(
    const json& authored, LayerPlan& layer) {
    if (!authored.contains("ef")) return std::nullopt;
    if (!authored["ef"].is_array() || authored["ef"].size() > 16)
        return std::string("Lottie layer effects must be an array with at most 16 entries");
    constexpr float blur_to_sigma = 0.3f;  // ThorVG Lottie compatibility conversion.
    constexpr float degrees_to_radians = 0.01745329251994329577f;
    for (const auto& effect : authored["ef"]) {
        if (!effect.is_object() || !effect.contains("ty") || !effect["ty"].is_number_integer())
            return std::string("Lottie effect entry is malformed");
        if (effect.contains("en") && !effect["en"].is_boolean() &&
            !effect["en"].is_number_integer())
            return std::string("Lottie effect enabled flag must be an integer or boolean");
        if (effect.contains("en") &&
            ((effect["en"].is_boolean() && !effect["en"].get<bool>()) ||
             (effect["en"].is_number_integer() && effect["en"].get<int>() == 0))) continue;
        const int type = effect["ty"].get<int>();
        std::vector<float> values;
        if (type == 29) {  // After Effects Gaussian Blur.
            float blurriness{}, direction{}, wrap{};
            AnimationTrackPlan blur_track;
            const bool blur_static = lottie_static_effect_value(effect, 0, values) &&
                                     values.size() == 1;
            if (!blur_static && (!effect.contains("ef") || !effect["ef"].is_array() ||
                effect["ef"].empty() || !lottie_decode_transform_track(
                    effect["ef"][0], "v", "lottie_blur", 1.0f, blur_track)))
                return std::string("malformed Lottie Gaussian Blur property");
            if (blur_static) blurriness = values[0];
            else blurriness = blur_track.keyframes.front().value.front();
            if (!lottie_static_effect_value(effect, 1, values) || values.size() != 1)
                return std::string("Lottie Gaussian Blur direction must be static");
            direction = values[0];
            if (!lottie_static_effect_value(effect, 2, values) || values.size() != 1)
                return std::string("Lottie Gaussian Blur edge mode must be static");
            wrap = values[0];
            if (blurriness < 0.0f || blurriness > 4096.0f ||
                blurriness * blur_to_sigma > 256.0f || direction != 1.0f || wrap != 0.0f)
                return std::string("Lottie Gaussian Blur supports bounded blur on both dimensions (direction 1) without edge wrapping");
            LayerPlan::EffectPlan lowered;
            lowered.kind = LayerPlan::EffectKindPlan::GaussianBlur;
            lowered.radius = blurriness * blur_to_sigma;
            layer.effects.push_back(std::move(lowered));
            if (!blur_static) {
                for (auto& key : blur_track.keyframes) {
                    if (key.value.size() != 1 || key.value.front() < 0.0f ||
                        key.value.front() * blur_to_sigma > 256.0f)
                        return std::string("animated Lottie Gaussian Blur radius exceeds native bounds");
                    key.value.front() *= blur_to_sigma;
                }
                layer.effect_param_tracks.push_back({"blur.gaussian", "radius",
                    std::move(blur_track)});
            }
        } else if (type == 25) {  // After Effects Drop Shadow.
            std::vector<float> color;
            if (!lottie_static_effect_value(effect, 0, color) ||
                (color.size() != 3 && color.size() != 4))
                return std::string("animated or malformed Lottie Drop Shadow color is unsupported");
            float opacity{}, angle{}, distance{}, softness{}, shadow_only{};
            const auto scalar = [&](std::size_t index, float& value) {
                if (!lottie_static_effect_value(effect, index, values) || values.size() != 1)
                    return false;
                value = values[0];
                return true;
            };
            if (!scalar(1, opacity) || !scalar(2, angle) || !scalar(3, distance) ||
                !scalar(5, shadow_only))
                return std::string("animated or malformed Lottie Drop Shadow parameters are unsupported");
            AnimationTrackPlan softness_track;
            const bool softness_static = lottie_static_effect_value(effect, 4, values) &&
                                         values.size() == 1;
            if (softness_static) softness = values[0];
            else if (!effect.contains("ef") || !effect["ef"].is_array() ||
                effect["ef"].size() <= 4 || !lottie_decode_transform_track(
                    effect["ef"][4], "v", "lottie_shadow_softness", 1.0f, softness_track))
                return std::string("malformed Lottie Drop Shadow softness property");
            else softness = softness_track.keyframes.front().value.front();
            if (std::any_of(color.begin(), color.end(), [](float c) { return c < 0.0f || c > 1.0f; }) ||
                opacity < 0.0f || opacity > 255.0f || angle < -360.0f || angle > 360.0f ||
                distance < 0.0f || distance > 10000.0f || softness < 0.0f || softness > 4096.0f ||
                shadow_only != 0.0f)
                return std::string("Lottie Drop Shadow values are outside the supported bounds");
            const float radians = angle * degrees_to_radians;
            LayerPlan::EffectPlan lowered;
            lowered.kind = LayerPlan::EffectKindPlan::DropShadow;
            lowered.radius = softness * blur_to_sigma;
            lowered.offset = {std::cos(radians) * distance,
                              std::sin(radians) * distance};
            lowered.effect_color = {
                srgb_to_linear(color[0]), srgb_to_linear(color[1]),
                srgb_to_linear(color[2]),
                (color.size() == 4 ? color[3] : 1.0f) * opacity / 255.0f};
            layer.effects.push_back(std::move(lowered));
            if (!softness_static) {
                for (auto& key : softness_track.keyframes) {
                    if (key.value.size() != 1 || key.value.front() < 0.0f ||
                        key.value.front() * blur_to_sigma > 256.0f)
                        return std::string("animated Lottie Drop Shadow softness exceeds native bounds");
                    key.value.front() *= blur_to_sigma;
                }
                layer.effect_param_tracks.push_back({"light.drop_shadow", "radius",
                    std::move(softness_track)});
            }
        } else if (type == 21) {  // After Effects Fill; type/parameter order confirmed in ThorVG.
            std::vector<float> color;
            float opacity{};
            AnimationTrackPlan color_track;
            AnimationTrackPlan opacity_track;
            const auto animated_parameter = [&](std::size_t index, const char* name,
                                                AnimationTrackPlan& track) {
                if (!effect.contains("ef") || !effect["ef"].is_array() ||
                    index >= effect["ef"].size()) return false;
                return lottie_decode_transform_track(effect["ef"][index], "v", name, 1.0f, track);
            };
            const bool color_static = lottie_static_effect_value(effect, 2, color);
            if (!color_static && !animated_parameter(2, "fill_color", color_track))
                return std::string("malformed Lottie Fill color property");
            const bool opacity_static = lottie_static_effect_value(effect, 6, values) &&
                values.size() == 1;
            if (!opacity_static && !animated_parameter(6, "fill_opacity", opacity_track))
                return std::string("malformed Lottie Fill opacity property");
            if (!color_static) color = color_track.keyframes.front().value;
            if (!opacity_static) opacity = opacity_track.keyframes.front().value.front();
            else opacity = values[0];
            if (color.size() != 4 ||
                std::any_of(color.begin(), color.end(), [](float c) { return c < 0.0f || c > 1.0f; }) ||
                opacity < 0.0f || opacity > 100.0f || std::abs(color[3] - 1.0f) > 1e-6f)
                return std::string("Lottie Fill values are outside the supported bounds");
            LayerPlan::EffectPlan lowered;
            lowered.kind = LayerPlan::EffectKindPlan::Fill;
            lowered.amount = opacity / 100.0f;
            lowered.fill_opacity_replaces_alpha = true;
            lowered.effect_color = {srgb_to_linear(color[0]), srgb_to_linear(color[1]),
                                    srgb_to_linear(color[2]), 1.0f};
            layer.effects.push_back(std::move(lowered));
            constexpr std::string_view fill_id = "generate.fill";
            const std::array<std::string_view, 3> channels{"color.r", "color.g", "color.b"};
            if (!color_static) {
                for (std::size_t channel = 0; channel < channels.size(); ++channel) {
                    auto track = color_track;
                    for (auto& key : track.keyframes) {
                        if (key.value.size() != 4 || key.value[3] != 1.0f ||
                            key.value[channel] < 0.0f || key.value[channel] > 1.0f)
                            return std::string("animated Lottie Fill color must use bounded RGB and opaque alpha");
                        key.value = {srgb_to_linear(key.value[channel])};
                    }
                    layer.effect_param_tracks.push_back({std::string(fill_id),
                        std::string(channels[channel]), std::move(track)});
                }
            }
            if (!opacity_static) {
                for (auto& key : opacity_track.keyframes) {
                    if (key.value.size() != 1 || key.value.front() < 0.0f || key.value.front() > 100.0f)
                        return std::string("animated Lottie Fill opacity must be in [0,100]");
                    key.value.front() /= 100.0f;
                }
                layer.effect_param_tracks.push_back({std::string(fill_id), "amount",
                    std::move(opacity_track)});
            }
        } else if (type == 20) {  // After Effects Tint (Black, White, Amount).
            LayerPlan::EffectPlan lowered;
            lowered.kind = LayerPlan::EffectKindPlan::Catalog;
            lowered.canonical_effect_id = "color.lottie_tint";
            constexpr std::array<std::string_view, 7> names{
                "black_r", "black_g", "black_b", "white_r", "white_g", "white_b", "amount"};
            const auto append_parameter = [&](std::size_t slot, std::size_t name_offset,
                                              std::size_t components, float divisor)
                                              -> std::optional<std::string> {
                if (lottie_static_effect_value(effect, slot, values)) {
                    if (values.size() != components)
                        return std::string("Lottie Tint parameter has unsupported dimensions");
                    for (std::size_t channel = 0; channel < components; ++channel) {
                        const float value = values[channel] / divisor;
                        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                            return std::string("Lottie Tint color/amount is outside supported bounds");
                        lowered.canonical_params.push_back({std::string(names[name_offset + channel]), value});
                    }
                    return std::nullopt;
                }
                if (!effect.contains("ef") || !effect["ef"].is_array() ||
                    slot >= effect["ef"].size())
                    return std::string("Lottie Tint parameter is missing or malformed");
                AnimationTrackPlan track;
                if (!lottie_decode_transform_track(effect["ef"][slot], "v",
                        "lottie_tint", 1.0f, track) || track.keyframes.empty())
                    return std::string("malformed or unsupported animated Lottie Tint parameter");
                const std::size_t value_components = track.keyframes.front().value.size();
                if (value_components != components)
                    return std::string("animated Lottie Tint parameter has unsupported dimensions");
                for (std::size_t channel = 0; channel < components; ++channel) {
                    auto component_track = track;
                    component_track.property = std::string(names[name_offset + channel]);
                    for (auto& key : component_track.keyframes) {
                        if (key.value.size() != components || !std::isfinite(key.value[channel]) ||
                            key.value[channel] < 0.0f || key.value[channel] > divisor)
                            return std::string("animated Lottie Tint color/amount is outside supported bounds");
                        key.value = {key.value[channel] / divisor};
                    }
                    lowered.canonical_params.push_back({std::string(names[name_offset + channel]),
                        component_track.keyframes.front().value.front()});
                    layer.effect_param_tracks.push_back({"color.lottie_tint",
                        std::string(names[name_offset + channel]), std::move(component_track)});
                }
                return std::nullopt;
            };
            // ThorVG parser slot order is black, white, amount. Lottie-web's
            // Tint implementation maps the averaged grayscale through these
            // colors, so preserve the authored controls and let the native
            // software color stage apply their exact transfer functions.
            if (auto error = append_parameter(0, 0, 3, 1.0f)) return *error;
            if (auto error = append_parameter(1, 3, 3, 1.0f)) return *error;
            if (auto error = append_parameter(2, 6, 1, 100.0f)) return *error;
            layer.effects.push_back(std::move(lowered));
        } else if (type == 23) {  // After Effects Tritone (bright, mid, dark, blend).
            LayerPlan::EffectPlan lowered;
            lowered.kind = LayerPlan::EffectKindPlan::Catalog;
            lowered.canonical_effect_id = "color.lottie_tritone";
            constexpr std::array<std::string_view, 10> names{
                "dark_r", "dark_g", "dark_b", "mid_r", "mid_g", "mid_b",
                "bright_r", "bright_g", "bright_b", "amount"};
            const auto append_parameter = [&](std::size_t slot, std::size_t name_offset,
                                              std::size_t components, float divisor)
                                              -> std::optional<std::string> {
                if (lottie_static_effect_value(effect, slot, values)) {
                    if (values.size() != components)
                        return std::string("Lottie Tritone parameter has unsupported dimensions");
                    for (std::size_t channel = 0; channel < components; ++channel) {
                        const float value = values[channel] / divisor;
                        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                            return std::string("Lottie Tritone color/blend is outside supported bounds");
                        lowered.canonical_params.push_back({std::string(names[name_offset + channel]), value});
                    }
                    return std::nullopt;
                }
                if (!effect.contains("ef") || !effect["ef"].is_array() || slot >= effect["ef"].size())
                    return std::string("Lottie Tritone parameter is missing or malformed");
                AnimationTrackPlan track;
                if (!lottie_decode_transform_track(effect["ef"][slot], "v", "lottie_tritone", 1.0f, track) ||
                    track.keyframes.empty())
                    return std::string("malformed or unsupported animated Lottie Tritone parameter");
                const std::size_t value_components = track.keyframes.front().value.size();
                if (value_components != components)
                    return std::string("animated Lottie Tritone parameter has unsupported dimensions");
                for (std::size_t channel = 0; channel < components; ++channel) {
                    auto component_track = track;
                    component_track.property = std::string(names[name_offset + channel]);
                    for (auto& key : component_track.keyframes) {
                        if (key.value.size() != components || !std::isfinite(key.value[channel]) ||
                            key.value[channel] < 0.0f || key.value[channel] > divisor)
                            return std::string("animated Lottie Tritone value is outside supported bounds");
                        key.value = {key.value[channel] / divisor};
                    }
                    lowered.canonical_params.push_back({std::string(names[name_offset + channel]),
                        component_track.keyframes.front().value.front()});
                    layer.effect_param_tracks.push_back({"color.lottie_tritone",
                        std::string(names[name_offset + channel]), std::move(component_track)});
                }
                return std::nullopt;
            };
            // ThorVG's pinned parser orders the controls bright, midtone, dark,
            // blend; the canonical plan stores dark-to-bright lookup samples.
            if (auto error = append_parameter(0, 6, 3, 1.0f)) return *error;
            if (auto error = append_parameter(1, 3, 3, 1.0f)) return *error;
            if (auto error = append_parameter(2, 0, 3, 1.0f)) return *error;
            if (auto error = append_parameter(3, 9, 1, 100.0f)) return *error;
            layer.effects.push_back(std::move(lowered));
        } else {
            return std::string("Lottie effect type is not supported by the native EffectPlan mapping");
        }
    }

    for (const auto& track : layer.effect_param_tracks) {
        for (const auto& keyframe : track.track.keyframes) {
            if (keyframe.interpolation &&
                *keyframe.interpolation != interpolation_name::kLinear &&
                *keyframe.interpolation != interpolation_name::kHold)
                return std::string("Lottie effect parameter easing supports only linear and hold keyframes");
        }
    }

    // The graph applies native layer effects after the layer transform. SVG
    // filters (and After Effects layer effects as exported by Bodymovin) are
    // evaluated in layer space before that transform. Fold the static 2D
    // transform into the effect parameters so a scaled layer also scales its
    // blur radius and shadow vector. This mirrors the compatibility mapping
    // used by ThorVG/Lottie-web without adding a renderer dependency.
    const bool has_geometric_effect = std::any_of(layer.effects.begin(), layer.effects.end(),
        [](const auto& effect) {
            return effect.kind == LayerPlan::EffectKindPlan::GaussianBlur ||
                   effect.kind == LayerPlan::EffectKindPlan::DropShadow;
        });
    if (!has_geometric_effect) return std::nullopt;
    if (!authored.contains("ks") || !authored["ks"].is_object())
        return std::string("Lottie effect layer transform is missing");
    std::vector<float> scale, rotation;
    bool scale_animated{}, rotation_animated{};
    if (!lottie_read_transform_property(authored["ks"], "s", scale, scale_animated) ||
        !lottie_read_transform_property(authored["ks"], "r", rotation, rotation_animated) ||
        scale.size() < 2 || scale.size() > 3 || rotation.size() != 1)
        return std::string("Lottie effect layer scale/rotation is malformed");
    if (scale_animated || rotation_animated)
        return std::string("Lottie blur/shadow under animated scale or rotation is not representable by the native effect tracks");
    const float sx = scale[0] / 100.0f;
    const float sy = scale[1] / 100.0f;
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.0f || sy <= 0.0f ||
        sx > 1000.0f || sy > 1000.0f)
        return std::string("Lottie effect layer scale is outside supported bounds");
    const float tolerance = 1e-5f * std::max({1.0f, sx, sy});
    const bool has_blur_radius = std::any_of(layer.effects.begin(), layer.effects.end(),
        [](const auto& effect) {
            return (effect.kind == LayerPlan::EffectKindPlan::GaussianBlur ||
                    effect.kind == LayerPlan::EffectKindPlan::DropShadow) && effect.radius > 0.0f;
        });
    if (has_blur_radius && std::abs(sx - sy) > tolerance)
        return std::string("Lottie blur/shadow with non-uniform layer scale is not representable by the native scalar radius");
    const float angle = rotation.front() * 0.01745329251994329577f;
    const float c = std::cos(angle), s = std::sin(angle);
    const float radius_scale = (sx + sy) * 0.5f;
    for (auto& effect : layer.effects) {
        if (effect.kind != LayerPlan::EffectKindPlan::GaussianBlur &&
            effect.kind != LayerPlan::EffectKindPlan::DropShadow) continue;
        effect.radius *= radius_scale;
        const float x = effect.offset[0] * sx;
        const float y = effect.offset[1] * sy;
        effect.offset = {c * x - s * y, s * x + c * y};
    }
    for (auto& param_track : layer.effect_param_tracks) {
        if (param_track.param != "radius" ||
            (param_track.effect_id != "blur.gaussian" && param_track.effect_id != "light.drop_shadow"))
            continue;
        for (auto& key : param_track.track.keyframes) {
            if (key.value.size() != 1)
                return std::string("Lottie animated blur/shadow radius must be scalar");
            key.value.front() *= radius_scale;
        }
    }
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
