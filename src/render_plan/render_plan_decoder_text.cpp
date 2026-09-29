#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {
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
    selector.wiggly_frequency = value.value("wiggly_frequency", 1.0f);
    selector.wiggly_amplitude = value.value("wiggly_amplitude", 0.25f);
    selector.wiggly_seed = value.value("wiggly_seed", std::uint64_t{0});
    selector.noise_frequency = value.value("noise_frequency", 1.0f);
    selector.noise_amplitude = value.value("noise_amplitude", 0.25f);
    selector.noise_seed = value.value("noise_seed", std::uint64_t{0});
    selector.index_step = value.value("index_step", std::uint32_t{1});
    const float modulation_frequency = selector.shape == "noise"
        ? selector.noise_frequency : selector.wiggly_frequency;
    const float modulation_amplitude = selector.shape == "noise"
        ? selector.noise_amplitude : selector.wiggly_amplitude;
    if ((selector.shape == "wiggly" || selector.shape == "noise") &&
        (!std::isfinite(modulation_frequency) || modulation_frequency <= 0.0f ||
         modulation_frequency > 1000.0f || !std::isfinite(modulation_amplitude) ||
         modulation_amplitude < 0.0f || modulation_amplitude > 1.0f))
        throw std::runtime_error(
            "text selector modulation frequency must be in (0, 1000] and amplitude in [0, 1]");
    if (selector.shape != "wiggly" && selector.shape != "noise" &&
        (value.contains("wiggly_frequency") || value.contains("wiggly_amplitude") ||
         value.contains("wiggly_seed")))
        throw std::runtime_error("wiggly selector parameters require shape='wiggly'");
    if (selector.shape == "noise" &&
        (value.contains("wiggly_frequency") || value.contains("wiggly_amplitude") ||
         value.contains("wiggly_seed")))
        throw std::runtime_error("wiggly selector parameters cannot configure shape='noise'");
    if (selector.shape != "noise" &&
        (value.contains("noise_frequency") || value.contains("noise_amplitude") ||
         value.contains("noise_seed")))
        throw std::runtime_error("noise selector parameters require shape='noise'");
    if (selector.order == "index") {
        if (selector.index_step == 0 || selector.index_step > 1000000)
            throw std::runtime_error("index selector index_step must be in [1, 1000000]");
        if (selector.randomize_order)
            throw std::runtime_error("index order cannot be combined with randomize_order");
    } else if (value.contains("index_step")) {
        throw std::runtime_error("index_step requires order='index'");
    }
    if (value.contains("semantic_id")) {
        selector.semantic_id = value.at("semantic_id").get<std::string>();
        if (selector.semantic_id->empty())
            throw std::runtime_error("text selector semantic_id must not be empty");
    }
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
        for (const auto& property : value.at("properties")) {
            auto track = decode_animation_track(property);
            if (track.property == "baseline_shift") {
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value[0]) ||
                        key.value[0] < -1000.0f || key.value[0] > 1000.0f) {
                        throw std::runtime_error("baseline_shift keyframes must be finite numbers in [-1000, 1000]");
                    }
                }
            } else if (track.property == "skew" || track.property == "skew_axis") {
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value[0]) ||
                        key.value[0] < -3600.0f || key.value[0] > 3600.0f) {
                        throw std::runtime_error(track.property + " keyframes must be finite numbers in [-3600, 3600]");
                    }
                }
            } else if (track.property == "stroke_width") {
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value[0]) ||
                        key.value[0] < 0.0f || key.value[0] > 1000.0f) {
                        throw std::runtime_error("stroke_width keyframes must be finite numbers in [0, 1000]");
                    }
                }
            } else if (track.property == "background_color" || track.property == "stroke_color") {
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 4) {
                        throw std::runtime_error(track.property + " keyframes require 4 RGBA components");
                    }
                    for (float comp : key.value) {
                        if (!std::isfinite(comp) || comp < 0.0f || comp > 1.0f) {
                            throw std::runtime_error(track.property + " color components must be in [0, 1]");
                        }
                    }
                }
            } else if (track.property == "character_offset") {
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value[0]) ||
                        key.value[0] < -10000.0f || key.value[0] > 10000.0f) {
                        throw std::runtime_error("character_offset keyframes must be finite integers in [-10000, 10000]");
                    }
                }
            } else if (track.property == "anchor") {
                for (const auto& key : track.keyframes) {
                    if (key.value.empty() || key.value.size() > 3) {
                        throw std::runtime_error("anchor keyframes require 1 to 3 components");
                    }
                    for (float comp : key.value) {
                        if (!std::isfinite(comp)) {
                            throw std::runtime_error("anchor keyframe values must be finite");
                        }
                    }
                }
            }
            animator.properties.push_back(std::move(track));
        }
    }
    return animator;
}

}  // namespace

void decode_layer_text_animators(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("text_animators")) {
        for (const auto& animator : value.at("text_animators"))
            layer.text_animators.push_back(decode_text_animator(animator));
    }
    for (const auto& animator : layer.text_animators) {
        for (const auto& selector : animator.selectors) {
            if (!selector.semantic_id) continue;
            const bool found = std::any_of(layer.text_spans.begin(), layer.text_spans.end(),
                [&](const TextSpanOverride& span) {
                    return span.semantic_id == selector.semantic_id;
                });
            if (!found)
                throw std::runtime_error("text animator selector references unknown semantic_id '" +
                                         *selector.semantic_id + "'");
        }
    }
}

}  // namespace chronon3d::render_plan::detail
