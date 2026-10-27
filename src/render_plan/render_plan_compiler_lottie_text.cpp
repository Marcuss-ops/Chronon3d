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

std::optional<std::string> decode_lottie_text_document(
    const LayerPlan& source, const json& document, const json& text,
    LayerPlan& layer) {
    if (!text.contains("t") || !text["t"].is_string() ||
        !text.contains("f") || !text["f"].is_string() ||
        !text.contains("s") || !text["s"].is_number())
        return std::string("Lottie static text requires string content, font, and numeric size");
    const auto content = text["t"].get<std::string>();
    const double font_size = text["s"].get<double>();
    if (content.empty() || content.size() > 16384 || !std::isfinite(font_size) ||
        font_size <= 0.0 || font_size > 4096.0)
        return std::string("Lottie text content or font size exceeds its supported bounds");
    const double tracking = text.value("tr", 0.0);
    const double leading = text.value("lh", 0.0);
    const int align = text.value("j", 0);
    if (!std::isfinite(tracking) || std::abs(tracking) > 10000.0 ||
        !std::isfinite(leading) || std::abs(leading) > 1e-6 ||
        align < 0 || align > 2 || text.contains("sz") || text.contains("ps") ||
        (text.contains("sw") && text["sw"].is_number() && text["sw"].get<double>() != 0.0))
        return std::string("Lottie text box, custom leading, or text stroke is not supported");

    const auto font_name = text["f"].get<std::string>();
    if (font_name.empty() || font_name.size() > 256)
        return std::string("Lottie text font name is empty or exceeds its bound");
    json font = json::object();
    if (document.contains("fonts") && document["fonts"].is_object() &&
        document["fonts"].contains("list") && document["fonts"]["list"].is_array()) {
        const auto found = std::find_if(document["fonts"]["list"].begin(),
            document["fonts"]["list"].end(), [&](const auto& candidate) {
                return candidate.is_object() && candidate.value("fName", std::string{}) == font_name;
            });
        if (found != document["fonts"]["list"].end()) font = *found;
    }

    auto& style = layer.style.emplace();
    style.font_family = font.value("fFamily", font_name);
    const auto font_style = font.value("fStyle", std::string("Regular"));
    style.font_style = font_style.find("Italic") != std::string::npos ? "italic" : "normal";
    style.font_weight = font_style.find("Black") != std::string::npos ? 900 :
        font_style.find("Bold") != std::string::npos ? 700 :
        font_style.find("Medium") != std::string::npos ? 500 :
        font_style.find("Light") != std::string::npos ? 300 : 400;
    style.font_size = static_cast<float>(font_size);
    style.fill = "#000000";
    style.fill_opacity = 1.0f;
    if (text.contains("fc")) {
        const auto& rgba = text["fc"];
        if (!rgba.is_array() || rgba.size() < 3 || rgba.size() > 4)
            return std::string("Lottie text fill color must contain three or four components");
        static constexpr char digits[] = "0123456789abcdef";
        std::string color = "#000000";
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (!rgba[channel].is_number())
                return std::string("Lottie text fill color components must be numeric");
            const double component = rgba[channel].get<double>();
            if (!std::isfinite(component) || component < 0.0 || component > 1.0)
                return std::string("Lottie text fill color components must be in [0,1]");
            const auto byte = static_cast<unsigned>(std::lround(component * 255.0));
            color[1 + channel * 2] = digits[(byte >> 4) & 0xf];
            color[2 + channel * 2] = digits[byte & 0xf];
        }
        style.fill = std::move(color);
        if (rgba.size() == 4) {
            if (!rgba[3].is_number())
                return std::string("Lottie text fill alpha must be numeric");
            const double alpha = rgba[3].get<double>();
            if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0)
                return std::string("Lottie text fill alpha must be in [0,1]");
            style.fill_opacity = static_cast<float>(alpha);
        }
    }
    style.text_align = align == 1 ? "right" : align == 2 ? "center" : "left";
    layer.text = content;
    if (tracking != 0.0) {
        chronon3d::TextSpanOverride span;
        span.byte_start = 0;
        span.byte_end = content.size();
        // ThorVG reads Bodymovin tracking in tenths of a pixel.
        span.tracking = static_cast<float>(tracking * 0.1);
        layer.text_spans.push_back(std::move(span));
    }
    if (font.contains("fPath") && font["fPath"].is_string() &&
        !font["fPath"].get<std::string>().empty()) {
        const std::filesystem::path font_path(font["fPath"].get<std::string>());
        const auto traverses = std::any_of(font_path.begin(), font_path.end(),
            [](const auto& component) { return component == ".."; });
        if (font_path.is_absolute() || traverses || !source.shape || !source.shape->lottie_asset)
            return std::string("Lottie font path must remain relative to the mounted asset root");
        const auto relative = (std::filesystem::path(*source.shape->lottie_asset).parent_path() /
            font_path).lexically_normal();
        if (relative.empty() || relative.is_absolute() || std::any_of(relative.begin(), relative.end(),
                [](const auto& component) { return component == ".."; }))
            return std::string("Lottie font path escapes the mounted asset root");
        layer.font = relative.generic_string();
    }
    return std::nullopt;
}

std::optional<std::string> decode_lottie_text_animators(
    const json& text_data, LayerPlan& layer) {
    if (!text_data.contains("a")) return std::nullopt;
    const auto& animators = text_data["a"];
    if (!animators.is_array() || animators.size() > 64)
        return std::string("Lottie text animator list must contain at most 64 entries");
    for (std::size_t index = 0; index < animators.size(); ++index) {
        const auto& authored = animators[index];
        if (!authored.is_object() || !authored.contains("s") || !authored["s"].is_object() ||
            !authored.contains("a") || !authored["a"].is_object())
            return std::string("Lottie text animator requires selector and property objects");
        const auto& selector = authored["s"];
        const auto& properties = authored["a"];
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if (it.key() != "p" && it.key() != "s" && it.key() != "r" &&
                it.key() != "o" && it.key() != "fc" && it.key() != "t")
                return std::string("Lottie text animator property is outside the bounded position/scale/rotation/opacity/fill/tracking subset");
        }
        if (properties.empty())
            return std::string("Lottie text animator has no supported animated properties");
        if (selector.value("b", 1) != 1)
            return std::string("Lottie text selector based-on mode is unsupported");

        const auto decode_scalar_track = [&](const json& object, const char* name,
                                             const char* property, float fallback,
                                             AnimationTrackPlan& track) -> bool {
            if (!object.contains(name)) {
                track.property = property;
                AnimationKeyframePlan key;
                key.frame = Frame{0};
                key.value = {fallback};
                track.keyframes.push_back(std::move(key));
                return true;
            }
            std::vector<float> values;
            bool animated{};
            if (!lottie_read_transform_property(object, name, values, animated)) return false;
            if (!animated) {
                if (values.size() != 1) return false;
                track.property = property;
                AnimationKeyframePlan key;
                key.frame = Frame{0};
                key.value = {values.front()};
                track.keyframes.push_back(std::move(key));
            } else if (!lottie_decode_transform_track(object, name, property, 1.0f, track)) {
                return false;
            }
            return !track.keyframes.empty() && std::all_of(
                track.keyframes.begin(), track.keyframes.end(), [](const auto& key) {
                    return key.value.size() == 1 && std::isfinite(key.value.front()) &&
                        (!key.interpolation ||
                         *key.interpolation == interpolation_name::kLinear ||
                         *key.interpolation == interpolation_name::kHold);
                });
        };
        AnimationTrackPlan start_track, end_track, offset_track, amount_track;
        if (!decode_scalar_track(selector, "s", "", 0.0f, start_track) ||
            !decode_scalar_track(selector, "e", "", 100.0f, end_track) ||
            !decode_scalar_track(selector, "o", "", 0.0f, offset_track) ||
            !decode_scalar_track(selector, "a", "", 100.0f, amount_track))
            return std::string("Lottie text selector ranges must use bounded linear/hold scalar tracks");
        const auto scalar_track_in_bounds = [](const AnimationTrackPlan& track,
                                               float minimum, float maximum) {
            return std::all_of(track.keyframes.begin(), track.keyframes.end(),
                [minimum, maximum](const auto& key) {
                    return key.value.front() >= minimum && key.value.front() <= maximum;
                });
        };
        if (!scalar_track_in_bounds(start_track, -1000.0f, 1000.0f) ||
            !scalar_track_in_bounds(end_track, -1000.0f, 1000.0f) ||
            !scalar_track_in_bounds(offset_track, -1000.0f, 1000.0f) ||
            !scalar_track_in_bounds(amount_track, 0.0f, 100.0f))
            return std::string("Lottie text selector ranges exceed supported bounds");
        const int based_on = selector.value("r", 1);
        const int shape = selector.value("sh", 1);
        if (based_on < 1 || based_on > 3 || shape < 1 || shape > 6)
            return std::string("Lottie text selector unit or shape is unsupported");
        const auto unsupported_selector_easing = [&](const char* name) -> bool {
            if (!selector.contains(name)) return false;
            std::vector<float> values;
            bool animated{};
            return !lottie_read_transform_property(selector, name, values, animated) ||
                animated || values.size() != 1 || std::abs(values.front()) > 1e-6f;
        };
        if (unsupported_selector_easing("xe") || unsupported_selector_easing("ne"))
            return std::string("Lottie text selector easing handles xe/ne are unsupported");
        float smoothness = 100.0f;
        if (selector.contains("sm")) {
            std::vector<float> values;
            bool animated{};
            if (!lottie_read_transform_property(selector, "sm", values, animated) || animated ||
                values.size() != 1) return std::string("Lottie text selector smoothness must be static");
            smoothness = values.front();
        }
        if (std::abs(smoothness - 100.0f) > 1e-6f)
            return std::string("Lottie text selector smoothness is unsupported");
        const int randomize = selector.value("rn", 0);
        if (randomize != 0 && randomize != 1)
            return std::string("Lottie text selector randomize flag must be 0 or 1");

        TextAnimatorPlan animator;
        animator.id = "lottie_text_animator_" + std::to_string(index);
        TextSelectorPlan selection;
        selection.id = animator.id + "_selector";
        selection.unit = based_on == 2 ? "word" : based_on == 3 ? "line" : "character";
        static constexpr std::array<std::string_view, 6> shapes{
            "square", "ramp_up", "ramp_down", "triangle", "round", "smooth"};
        selection.shape = std::string(shapes[static_cast<std::size_t>(shape - 1)]);
        selection.order = randomize ? "random" : "forward";
        selection.randomize_order = randomize != 0;
        selection.exclude_spaces = false;
        selection.end_inclusive = true;
        selection.start = std::move(start_track);
        selection.end = std::move(end_track);
        selection.offset = std::move(offset_track);
        selection.amount = std::move(amount_track);
        animator.selectors.push_back(std::move(selection));

        const auto add_property = [&](const char* source, const char* target,
                                      float divisor, std::size_t minimum_components,
                                      std::size_t maximum_components) -> bool {
            if (!properties.contains(source)) return true;
            std::vector<float> values;
            bool animated{};
            AnimationTrackPlan track;
            if (!lottie_read_transform_property(properties, source, values, animated)) return false;
            if (!animated) {
                if (values.size() < minimum_components || values.size() > maximum_components)
                    return false;
                track.property = target;
                AnimationKeyframePlan key;
                key.frame = Frame{0};
                for (const float value : values) {
                    const float scaled = value / divisor;
                    if (!std::isfinite(scaled)) return false;
                    key.value.push_back(scaled);
                }
                track.keyframes.push_back(std::move(key));
            } else if (!lottie_decode_transform_track(properties, source, target, divisor, track)) {
                return false;
            }
            if (track.keyframes.empty() || std::any_of(
                    track.keyframes.begin(), track.keyframes.end(), [&](const auto& key) {
                        return key.value.size() < minimum_components ||
                            key.value.size() > maximum_components ||
                            std::any_of(key.value.begin(), key.value.end(), [](float value) {
                                return !std::isfinite(value);
                            }) || (key.interpolation &&
                                   *key.interpolation != interpolation_name::kLinear &&
                                   *key.interpolation != interpolation_name::kHold);
                    })) return false;
            animator.properties.push_back(std::move(track));
            return true;
        };
        if (!add_property("p", "position", 1.0f, 2, 3) ||
            !add_property("s", "scale", 100.0f, 1, 3) ||
            !add_property("r", "rotation", 1.0f, 1, 1) ||
            !add_property("o", "opacity", 100.0f, 1, 1) ||
            !add_property("fc", "fill_color", 1.0f, 4, 4) ||
            !add_property("t", "tracking", 1.0f, 1, 1))
            return std::string("Lottie text animator properties must use bounded linear/hold tracks and supported dimensions");
        layer.text_animators.push_back(std::move(animator));
    }
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
