#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {
void reject_unknown_fields(const nlohmann::json& value,
                           std::initializer_list<std::string_view> allowed,
                           std::string_view field) {
    if (!value.is_object())
        throw std::runtime_error(std::string(field) + " must be an object");
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            throw std::runtime_error(std::string(field) + " contains unsupported field '" + it.key() + "'");
    }
}

void validate_text_track(const AnimationTrackPlan& track,
                         const std::vector<std::size_t>& expected_arities,
                         std::string_view field) {
    if (track.keyframes.empty() || track.keyframes.size() > 65536)
        throw std::runtime_error(std::string(field) + " must contain 1..65536 keyframes");
    std::vector<std::int64_t> frames;
    frames.reserve(track.keyframes.size());
    for (const auto& key : track.keyframes) {
        if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
            std::find(expected_arities.begin(), expected_arities.end(), key.value.size()) == expected_arities.end() ||
            !std::all_of(key.value.begin(), key.value.end(),
                         [](float value) { return std::isfinite(value); }))
            throw std::runtime_error(std::string(field) + " has invalid frame, value arity, or non-finite component");
        frames.push_back(key.frame.integral());
    }
    std::sort(frames.begin(), frames.end());
    if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
        throw std::runtime_error(std::string(field) + " keyframe frames must be unique");
    for (const auto& key : track.keyframes) {
        if (key.easing || key.interpolation || key.easing_xy || key.bezier_curve || key.spring ||
            key.roving || key.temporal_in_dx || key.temporal_in_dy ||
            key.temporal_out_dx || key.temporal_out_dy)
            throw std::runtime_error(std::string(field) + " keyframes cannot carry timing overrides");
    }
}

TextSelectorPlan decode_text_selector(const nlohmann::json& value) {
    reject_unknown_fields(value, {"id", "semantic_id", "unit", "shape", "order", "combine",
        "start", "end", "offset", "amount", "exclude_spaces", "randomize_order", "random_seed",
        "wiggly_frequency", "wiggly_amplitude", "wiggly_seed", "noise_frequency",
        "noise_amplitude", "noise_seed", "index_step"}, "text selector");
    if (value.contains("id") && (!value.at("id").is_string() || value.at("id").get<std::string>().size() > 256))
        throw std::runtime_error("text selector id must be a string of at most 256 bytes");
    TextSelectorPlan selector;
    selector.id = value.value("id", std::string{});
    selector.unit = value.value("unit", std::string{"glyph"});
    selector.shape = value.value("shape", std::string{"smooth"});
    selector.order = value.value("order", std::string{"forward"});
    selector.combine = value.value("combine", std::string{"replace"});
    static constexpr std::array<std::string_view, 5> units{"glyph", "grapheme", "character", "word", "line"};
    static constexpr std::array<std::string_view, 8> shapes{
        "square", "ramp_up", "ramp_down", "triangle", "round", "smooth", "wiggly", "noise"};
    static constexpr std::array<std::string_view, 6> orders{
        "forward", "reverse", "from_center", "to_center", "random", "index"};
    static constexpr std::array<std::string_view, 6> combines{
        "replace", "add", "subtract", "intersect", "min", "max"};
    if (std::find(units.begin(), units.end(), selector.unit) == units.end() ||
        std::find(shapes.begin(), shapes.end(), selector.shape) == shapes.end() ||
        std::find(orders.begin(), orders.end(), selector.order) == orders.end() ||
        std::find(combines.begin(), combines.end(), selector.combine) == combines.end())
        throw std::runtime_error("text selector has an unsupported unit, shape, order, or combine mode");
    if (value.contains("start")) selector.start = decode_animation_track(value.at("start"));
    if (value.contains("end")) selector.end = decode_animation_track(value.at("end"));
    if (value.contains("offset")) selector.offset = decode_animation_track(value.at("offset"));
    if (value.contains("amount")) selector.amount = decode_animation_track(value.at("amount"));
    for (const auto& [name, track] : std::array<std::pair<const char*, const AnimationTrackPlan*>, 4>{{
             {"start", &selector.start}, {"end", &selector.end},
             {"offset", &selector.offset}, {"amount", &selector.amount}}}) {
        if (!value.contains(name)) continue;
        if (!track->property.empty())
            throw std::runtime_error(std::string("text selector ") + name + " track cannot declare property");
        if (track->easing != "linear")
            throw std::runtime_error(std::string("text selector ") + name + " track supports linear baked timing only");
        if (track->loop_mode != "none" || track->spring || track->bezier_curve || track->wiggle ||
            track->oscillate || track->random_hold || track->posterize_time ||
            track->data_feed || track->audio_react)
            throw std::runtime_error(std::string("text selector ") + name + " track supports only baked explicit keyframes");
        validate_text_track(*track, {1}, std::string("text selector ") + name);
    }
    if ((value.contains("exclude_spaces") && !value.at("exclude_spaces").is_boolean()) ||
        (value.contains("randomize_order") && !value.at("randomize_order").is_boolean()))
        throw std::runtime_error("text selector exclude_spaces and randomize_order must be booleans");
    selector.exclude_spaces = value.value("exclude_spaces", true);
    selector.randomize_order = value.value("randomize_order", false);
    const auto bounded_seed = [&](const char* key) {
        if (!value.contains(key)) return std::uint64_t{0};
        const auto& raw = value.at(key);
        if (!raw.is_number_integer())
            throw std::runtime_error(std::string("text selector ") + key + " must be a uint32 value");
        if (raw.is_number_unsigned()) {
            const auto seed = raw.get<std::uint64_t>();
            if (seed > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error(std::string("text selector ") + key + " must be a uint32 value");
            return seed;
        }
        const auto seed = raw.get<std::int64_t>();
        if (seed < 0 || static_cast<std::uint64_t>(seed) > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error(std::string("text selector ") + key + " must be a uint32 value");
        return static_cast<std::uint64_t>(seed);
    };
    selector.random_seed = bounded_seed("random_seed");
    selector.wiggly_frequency = value.value("wiggly_frequency", 1.0f);
    selector.wiggly_amplitude = value.value("wiggly_amplitude", 0.25f);
    selector.wiggly_seed = bounded_seed("wiggly_seed");
    selector.noise_frequency = value.value("noise_frequency", 1.0f);
    selector.noise_amplitude = value.value("noise_amplitude", 0.25f);
    selector.noise_seed = bounded_seed("noise_seed");
    if (value.contains("index_step")) {
        const auto& raw_step = value.at("index_step");
        if (raw_step.is_number_unsigned()) {
            const auto step = raw_step.get<std::uint64_t>();
            if (step == 0 || step > 1000000U)
                throw std::runtime_error("text selector index_step must be an integer in [1, 1000000]");
            selector.index_step = static_cast<std::uint32_t>(step);
        } else {
            if (!raw_step.is_number_integer())
                throw std::runtime_error("text selector index_step must be an integer in [1, 1000000]");
            const auto step = raw_step.get<std::int64_t>();
            if (step < 1 || step > 1000000)
                throw std::runtime_error("text selector index_step must be an integer in [1, 1000000]");
            selector.index_step = static_cast<std::uint32_t>(step);
        }
    }
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
    if (selector.shape != "wiggly" &&
        (selector.wiggly_frequency != 1.0f || selector.wiggly_amplitude != 0.25f || selector.wiggly_seed != 0))
        throw std::runtime_error("wiggly selector parameters require shape='wiggly'");
    if (selector.shape != "noise" &&
        (selector.noise_frequency != 1.0f || selector.noise_amplitude != 0.25f || selector.noise_seed != 0))
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
        if (!value.at("semantic_id").is_string())
            throw std::runtime_error("text selector semantic_id must be a string");
        selector.semantic_id = value.at("semantic_id").get<std::string>();
        if (selector.semantic_id->empty() || selector.semantic_id->size() > 256)
            throw std::runtime_error("text selector semantic_id must contain 1..256 bytes");
    }
    return selector;
}

TextAnimatorPlan decode_text_animator(const nlohmann::json& value) {
    reject_unknown_fields(value, {"id", "selectors", "properties"}, "text animator");
    if (!value.contains("selectors") || !value.at("selectors").is_array() ||
        value.at("selectors").empty() || value.at("selectors").size() > 65536 ||
        !value.contains("properties") || !value.at("properties").is_array() ||
        value.at("properties").empty() || value.at("properties").size() > 65536)
        throw std::runtime_error("text animator requires 1..65536 selectors and property tracks");
    if (value.contains("id") && (!value.at("id").is_string() || value.at("id").get<std::string>().size() > 256))
        throw std::runtime_error("text animator id must be a string of at most 256 bytes");
    TextAnimatorPlan animator;
    animator.id = value.value("id", std::string{});
    if (value.contains("selectors")) {
        std::vector<std::string> selector_ids;
        for (const auto& selector : value.at("selectors")) {
            auto decoded = decode_text_selector(selector);
            if (!decoded.id.empty()) {
                if (std::find(selector_ids.begin(), selector_ids.end(), decoded.id) != selector_ids.end())
                    throw std::runtime_error("text selector ids must be unique per animator");
                selector_ids.push_back(decoded.id);
            }
            animator.selectors.push_back(std::move(decoded));
        }
    }

    if (value.contains("properties")) {
        for (const auto& property : value.at("properties")) {
            reject_unknown_fields(property, {"property", "easing", "loop_mode", "bezier_curve", "spring",
                "wiggle", "oscillate", "random_hold", "posterize_time", "keyframes"},
                "text animator property track");
            auto track = decode_animation_track(property);
            const auto allowed_property = [](const std::string& property) {
                static constexpr std::array<std::string_view, 21> properties{
                    "position", "position_x", "position_y", "scale", "scale_x", "scale_y",
                    "opacity", "blur", "tracking", "rotation", "fill_color", "skew",
                    "skew_axis", "anchor", "baseline_shift", "background_color",
                    "background_padding_em", "background_radius_em", "stroke_color",
                    "stroke_width", "character_offset"};
                return std::find(properties.begin(), properties.end(), property) != properties.end();
            };
            if (!allowed_property(track.property))
                throw std::runtime_error("unsupported text animator property: " + track.property);
            const std::vector<std::size_t> arities = track.property == "position"
                ? std::vector<std::size_t>{2, 3}
                : track.property == "scale"
                    ? std::vector<std::size_t>{1, 2, 3}
                    : track.property == "rotation"
                        ? std::vector<std::size_t>{1, 3}
                        : track.property == "anchor"
                            ? std::vector<std::size_t>{1, 2, 3}
                            : (track.property == "fill_color" || track.property == "background_color" ||
                               track.property == "stroke_color")
                                ? std::vector<std::size_t>{4}
                                : std::vector<std::size_t>{1};
            validate_text_track(track, arities, "text animator property");
            if (track.spring || track.bezier_curve || track.loop_mode != "none" || track.wiggle ||
                track.oscillate || track.random_hold || track.posterize_time || track.data_feed || track.audio_react)
                throw std::runtime_error("text animator property tracks do not accept timing loops or procedural drivers");
            if (track.easing != "linear")
                throw std::runtime_error("text animator property tracks require sampled linear keyframes");
            for (const auto& key : track.keyframes) {
                const float scalar = key.value.size() == 1 ? key.value.front() : 0.f;
                if ((track.property == "opacity" && (scalar < 0.f || scalar > 1.f)) ||
                    (track.property == "blur" && scalar < 0.f) ||
                    (track.property == "tracking" && (scalar < -1000.f || scalar > 1000.f)) ||
                    (track.property == "baseline_shift" && (scalar < -1000.f || scalar > 1000.f)) ||
                    (track.property == "skew" && (scalar < -80.f || scalar > 80.f)) ||
                    (track.property == "skew_axis" && (scalar < -1000000.f || scalar > 1000000.f)) ||
                    ((track.property == "background_padding_em" || track.property == "background_radius_em") &&
                     (scalar < 0.f || scalar > 128.f)) ||
                    (track.property == "stroke_width" && (scalar < 0.f || scalar > 256.f)))
                    throw std::runtime_error(track.property + " keyframe value is outside RenderPlan v3 bounds");
                if (track.property == "fill_color" || track.property == "background_color" ||
                    track.property == "stroke_color")
                    for (const float component : key.value)
                        if (component < 0.f || component > 1.f)
                            throw std::runtime_error(track.property + " color components must be in [0, 1]");
            }
            if (track.property == "character_offset") {
                const auto& key = track.keyframes.front();
                if (key.value[0] < -10000.0f || key.value[0] > 10000.0f ||
                    std::floor(key.value[0]) != key.value[0] || key.frame.integral() != 0 ||
                    track.keyframes.size() != 1)
                    throw std::runtime_error("character_offset requires one static integer keyframe at frame 0 in [-10000, 10000]");
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
            if (std::any_of(animator.properties.begin(), animator.properties.end(),
                    [&](const AnimationTrackPlan& previous) { return previous.property == track.property; }))
                throw std::runtime_error("text animator property tracks must be unique");
            if (track.property == "anchor" && track.keyframes.size() > 1)
                throw std::runtime_error("animated text animator anchor is not supported by the text compiler");
            if ((track.property == "position" || track.property == "position_x" ||
                 track.property == "position_y" || track.property == "scale" ||
                 track.property == "scale_x" || track.property == "scale_y" ||
                 track.property == "rotation" || track.property == "anchor") &&
                std::any_of(track.keyframes.begin(), track.keyframes.end(),
                    [](const AnimationKeyframePlan& key) {
                        return std::any_of(key.value.begin(), key.value.end(),
                            [](float component) { return std::abs(component) > 1000000.f; });
                    }))
                throw std::runtime_error(track.property + " components must be within [-1000000, 1000000]");
            animator.properties.push_back(std::move(track));
        }
    }
    return animator;
}

}  // namespace

void decode_layer_text_animators(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("text_animators")) {
        if (layer.type != LayerType::Text)
            throw std::runtime_error("text_animators are only valid on text layers");
        if (!value.at("text_animators").is_array() || value.at("text_animators").size() > 65536)
            throw std::runtime_error("text_animators must be an array of at most 65536 entries");
        std::vector<std::string> animator_ids;
        for (const auto& animator : value.at("text_animators")) {
            auto decoded = decode_text_animator(animator);
            if (!decoded.id.empty()) {
                if (std::find(animator_ids.begin(), animator_ids.end(), decoded.id) != animator_ids.end())
                    throw std::runtime_error("text animator ids must be unique per layer");
                animator_ids.push_back(decoded.id);
            }
            layer.text_animators.push_back(std::move(decoded));
        }
    }
    if (layer.text_spans.size() > 65536)
        throw std::runtime_error("text spans must contain at most 65536 entries");
    std::vector<std::string> semantic_ids;
    for (const auto& span : layer.text_spans) {
        if (!span.semantic_id) continue;
        if (std::find(semantic_ids.begin(), semantic_ids.end(), *span.semantic_id) != semantic_ids.end())
            throw std::runtime_error("text span semantic_id values must be unique per layer");
        semantic_ids.push_back(*span.semantic_id);
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
