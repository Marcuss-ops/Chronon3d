#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {

void reject_unknown_driver_fields(const nlohmann::json& value,
                                  std::string_view driver_name,
                                  std::initializer_list<std::string_view> allowed) {
    if (!value.is_object())
        throw std::runtime_error(std::string(driver_name) + " driver must be an object");
    for (auto field = value.begin(); field != value.end(); ++field) {
        const bool known = std::find(allowed.begin(), allowed.end(), field.key()) != allowed.end();
        if (!known)
            throw std::runtime_error(std::string(driver_name) +
                " driver contains an unsupported field: " + field.key());
    }
}

bool scalar_driver_property(std::string_view property) {
    static constexpr std::array<std::string_view, 18> direct{
        "opacity", "blur", "tracking", "stroke_dash_offset", "stroke_width",
        "position_x", "position_y", "position_z", "scale_x", "scale_y", "scale_z",
        "rotation_x", "rotation_y", "rotation_z",
        "skew", "skew_axis", "baseline_shift", "character_offset"};
    if (std::find(direct.begin(), direct.end(), property) != direct.end()) return true;
    for (const auto suffix : {"_opacity", "_blur", "_offset_x", "_offset_y"}) {
        if (!property.starts_with("shadow_") || !property.ends_with(suffix)) continue;
        const auto index = property.substr(7, property.size() - 7 - std::char_traits<char>::length(suffix));
        if (index == "0" || index == "1" || index == "2") return true;
    }
    return false;
}
SpringPlan decode_spring(const nlohmann::json& value, std::string_view field) {
    if (!value.is_object())
        throw std::runtime_error(std::string(field) + " must be an object");
    SpringPlan spring;
    spring.mass = value.value("mass", spring.mass);
    spring.stiffness = value.value("stiffness", spring.stiffness);
    spring.damping = value.value("damping", spring.damping);
    spring.initial_velocity = value.value("initial_velocity", spring.initial_velocity);
    if (!is_valid_spring_config(spring) || spring.mass > 10000.0f ||
        spring.stiffness > 100000.0f || spring.damping > 100000.0f ||
        std::abs(spring.initial_velocity) > 1000000.0f)
        throw std::runtime_error(std::string(field) + " has invalid mass, stiffness, damping, or initial_velocity");
    return spring;
}

AnimationTrackPlan decode_animation_track_impl(const nlohmann::json& value) {
    AnimationTrackPlan track;
    // 'property' is optional: text-selector bounds (start/end/offset/amount)
    // carry it for backward compatibility but never use it semantically, so a
    // selector track without a property is valid.  Layer-animation and text-
    // animator property tracks require it at the schema level; a missing one
    // surfaces as "unsupported property" during compilation instead of a
    // confusing decode failure.
    track.property = value.value("property", std::string{});
    track.easing = value.value("easing", std::string{"linear"});
    track.loop_mode = value.value("loop_mode", std::string{"none"});
    if (track.loop_mode != "none" && track.loop_mode != "cycle" &&
        track.loop_mode != "pingpong" && track.loop_mode != "offset")
        throw std::runtime_error("unsupported animation loop_mode: " + track.loop_mode);
    if (track.loop_mode == "offset" &&
        (track.property == "fill_color" || track.property == "background_color" ||
         track.property == "stroke_color"))
        throw std::runtime_error("animation loop_mode='offset' requires a numeric or vector property");
    if (value.contains("wiggle")) {
        const auto& raw_wiggle = value.at("wiggle");
        if (!raw_wiggle.is_object())
            throw std::runtime_error("animation wiggle driver must be an object");
        for (auto field = raw_wiggle.begin(); field != raw_wiggle.end(); ++field)
            if (field.key() != "frequency" && field.key() != "amplitude" && field.key() != "seed")
                throw std::runtime_error("animation wiggle driver contains an unsupported field: " + field.key());
        AnimationTrackPlan::WiggleDriverPlan driver;
        driver.frequency = raw_wiggle.value("frequency", driver.frequency);
        driver.amplitude = raw_wiggle.value("amplitude", driver.amplitude);
        driver.seed = raw_wiggle.value("seed", driver.seed);
        if (!std::isfinite(driver.frequency) || driver.frequency <= 0.0f ||
            driver.frequency > 1000.0f || !std::isfinite(driver.amplitude) ||
            std::abs(driver.amplitude) > 10000.0f)
            throw std::runtime_error("animation wiggle frequency/amplitude is outside supported bounds");
        if (track.property != "opacity" && track.property != "blur" &&
            track.property != "tracking" && track.property != "stroke_dash_offset" &&
            track.property != "skew" && track.property != "skew_axis" &&
            track.property != "baseline_shift" && track.property != "stroke_width")
            throw std::runtime_error("animation wiggle driver requires a scalar opacity, blur, tracking, stroke_dash_offset, skew, baseline_shift, or stroke_width property");
        track.wiggle = driver;
    }
    const auto driver_count = static_cast<int>(value.contains("wiggle")) +
        static_cast<int>(value.contains("oscillate")) +
        static_cast<int>(value.contains("random_hold")) +
        static_cast<int>(value.contains("posterize_time")) +
        static_cast<int>(value.contains("data_feed")) +
        static_cast<int>(value.contains("audio_react"));
    if (driver_count > 1)
        throw std::runtime_error("an animation track can contain only one procedural driver");
    if ((value.contains("data_feed") || value.contains("audio_react")) && value.contains("keyframes"))
        throw std::runtime_error("data_feed and audio_react tracks cannot also declare keyframes");
    if (driver_count > 0 && !scalar_driver_property(track.property))
        throw std::runtime_error("procedural animation drivers require a supported scalar property");
    if (value.contains("oscillate")) {
        const auto& raw = value.at("oscillate");
        reject_unknown_driver_fields(raw, "oscillate",
            {"frequency_hz", "amplitude", "phase_deg", "waveform", "offset"});
        AnimationTrackPlan::OscillateDriverPlan driver;
        driver.frequency_hz = raw.value("frequency_hz", driver.frequency_hz);
        driver.amplitude = raw.value("amplitude", driver.amplitude);
        driver.phase_deg = raw.value("phase_deg", driver.phase_deg);
        driver.waveform = raw.value("waveform", driver.waveform);
        driver.offset = raw.value("offset", driver.offset);
        if (!std::isfinite(driver.frequency_hz) || driver.frequency_hz < 0.001f ||
            driver.frequency_hz > 60.0f || !std::isfinite(driver.amplitude) ||
            !std::isfinite(driver.phase_deg) || !std::isfinite(driver.offset) ||
            (driver.waveform != "sine" && driver.waveform != "triangle" &&
             driver.waveform != "sawtooth" && driver.waveform != "square"))
            throw std::runtime_error("oscillate driver parameters are outside supported bounds");
        track.oscillate = std::move(driver);
    }
    if (value.contains("random_hold")) {
        const auto& raw = value.at("random_hold");
        reject_unknown_driver_fields(raw, "random_hold",
            {"interval_frames", "min", "max", "seed"});
        AnimationTrackPlan::RandomHoldDriverPlan driver;
        driver.interval_frames = raw.value("interval_frames", driver.interval_frames);
        driver.min = raw.value("min", driver.min);
        driver.max = raw.value("max", driver.max);
        driver.seed = raw.value("seed", driver.seed);
        if (driver.interval_frames == 0 || driver.interval_frames > 600 ||
            !std::isfinite(driver.min) || !std::isfinite(driver.max) ||
            driver.min > driver.max)
            throw std::runtime_error("random_hold driver parameters are outside supported bounds");
        track.random_hold = std::move(driver);
    }
    if (value.contains("posterize_time")) {
        const auto& raw = value.at("posterize_time");
        reject_unknown_driver_fields(raw, "posterize_time", {"fps"});
        AnimationTrackPlan::PosterizeTimeDriverPlan driver;
        driver.fps = raw.value("fps", driver.fps);
        if (!std::isfinite(driver.fps) || driver.fps < 1.0f || driver.fps > 30.0f)
            throw std::runtime_error("posterize_time fps must be finite and within [1,30]");
        track.posterize_time = std::move(driver);
    }
    if (value.contains("data_feed")) {
        const auto& raw = value.at("data_feed");
        reject_unknown_driver_fields(raw, "data_feed", {"values", "asset", "sha256", "item_count", "index_offset", "interpolation"});
        AnimationTrackPlan::DataFeedDriverPlan driver;
        driver.index_offset = raw.value("index_offset", std::int32_t{0});
        driver.interpolation = raw.value("interpolation", std::string{"step"});
        if (driver.index_offset < -1000000 || driver.index_offset > 1000000 ||
            (driver.interpolation != "step" && driver.interpolation != "linear"))
            throw std::runtime_error("data_feed offset or interpolation is outside supported bounds");
        if (raw.contains("values")) {
            if (raw.contains("asset") || raw.contains("sha256") || raw.contains("item_count") ||
                !raw.at("values").is_array() || raw.at("values").empty() || raw.at("values").size() > 1000000)
                throw std::runtime_error("data_feed must contain either 1 to 1000000 inline values or an asset descriptor");
            driver.values.reserve(raw.at("values").size());
            for (const auto& sample : raw.at("values")) {
                if (!sample.is_number() || !std::isfinite(sample.get<double>()))
                    throw std::runtime_error("data_feed values must be finite numbers");
                driver.values.push_back(sample.get<float>());
                if (!std::isfinite(driver.values.back()))
                    throw std::runtime_error("data_feed values must fit finite float range");
            }
        } else {
            if (!raw.contains("asset") || !raw.contains("sha256") || !raw.contains("item_count") ||
                !raw.at("asset").is_string() || !raw.at("sha256").is_string() ||
                !raw.at("item_count").is_number_integer())
                throw std::runtime_error("data_feed asset requires asset, sha256, and item_count");
            driver.asset = raw.at("asset").get<std::string>();
            driver.sha256 = raw.at("sha256").get<std::string>();
            const auto item_count = raw.at("item_count").get<std::int64_t>();
            if (item_count < 1 || item_count > 1000000)
                throw std::runtime_error("data_feed asset item_count must be within [1,1000000]");
            driver.item_count = static_cast<std::uint32_t>(item_count);
            if (invalid_logical_path(driver.asset) ||
                driver.sha256.size() != 64 || !std::all_of(driver.sha256.begin(), driver.sha256.end(),
                    [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
                throw std::runtime_error("data_feed asset path, digest, or item_count is invalid");
        }
        track.data_feed = std::move(driver);
    }
    if (value.contains("audio_react")) {
        const auto& raw = value.at("audio_react");
        reject_unknown_driver_fields(raw, "audio_react",
            {"source", "feature", "band_hz", "smoothing_frames", "gain", "map"});
        if (!raw.contains("source") || !raw.at("source").is_string())
            throw std::runtime_error("audio_react requires a source clip reference");
        AnimationTrackPlan::AudioReactDriverPlan driver;
        driver.source = raw.at("source").get<std::string>();
        driver.feature = raw.value("feature", driver.feature);
        driver.smoothing_frames = raw.value("smoothing_frames", driver.smoothing_frames);
        driver.gain = raw.value("gain", driver.gain);
        driver.map = raw.value("map", driver.map);
        if (driver.source.size() < 5 || !driver.source.starts_with("mix:") ||
            driver.feature != "rms" || driver.smoothing_frames == 0 ||
            driver.smoothing_frames > 120 || !std::isfinite(driver.gain) ||
            driver.gain < 0.0f || driver.gain > 100.0f || driver.map != "linear")
            throw std::runtime_error("audio_react source, feature, smoothing, gain, or map is unsupported");
        if (raw.contains("band_hz")) {
            const auto& band = raw.at("band_hz");
            if (!band.is_array() || band.size() != 2 || !band[0].is_number() || !band[1].is_number())
                throw std::runtime_error("audio_react band_hz requires [low, high]");
            const float low = band[0].get<float>();
            const float high = band[1].get<float>();
            if (!std::isfinite(low) || !std::isfinite(high) || low < 1.0f ||
                high <= low || high > 96000.0f)
                throw std::runtime_error("audio_react band_hz must satisfy 1 <= low < high <= 96000");
            driver.band_hz = std::array<float, 2>{low, high};
        }
        if (track.property.empty() || !scalar_driver_property(track.property))
            throw std::runtime_error("audio_react requires a supported scalar animation property");
        track.audio_react = std::move(driver);
    }
    if (track.easing != "linear" && track.easing != "in_quad" && track.easing != "out_quad" &&
        track.easing != "in_out_quad" && track.easing != "in_cubic" &&
        track.easing != "out_cubic" && track.easing != "in_out_cubic" &&
        track.easing != "in_expo" && track.easing != "out_expo" &&
        track.easing != "in_out_expo" && track.easing != "in_sine" &&
        track.easing != "out_sine" && track.easing != "in_out_sine" &&
        track.easing != "in_back" && track.easing != "out_back" &&
        track.easing != "in_out_back" && track.easing != "in_elastic" &&
        track.easing != "out_elastic" && track.easing != "in_out_elastic" &&
        track.easing != "in_bounce" && track.easing != "out_bounce" &&
        track.easing != "in_out_bounce" && track.easing != "smoothstep" &&
        track.easing != "hold" && track.easing != "bezier" && track.easing != "spring")
        throw std::runtime_error("unsupported animation easing: " + track.easing);
    if (value.contains("spring")) track.spring = decode_spring(value.at("spring"), "animation track spring");
    if (track.easing != "spring" && track.spring)
        throw std::runtime_error("animation spring config requires easing='spring'");
    track.bezier_curve = decode_bezier_curve(value, track.easing, "animation track");
    if (value.contains("keyframes")) for (const auto& key_value : value.at("keyframes")) {
        AnimationKeyframePlan key;
        key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
        const auto& raw = key_value.at("value");
        if (raw.is_array()) {
            for (const auto& component : raw) key.value.push_back(component.get<float>());
        } else {
            key.value.push_back(raw.get<float>());
        }
        if (key_value.contains("interpolation"))
            key.interpolation = key_value.at("interpolation").get<std::string>();
        if (key_value.contains("easing"))
            key.easing = key_value.at("easing").get<std::string>();
        if (key_value.contains("easing_xy")) {
            const auto& easing_xy = key_value.at("easing_xy");
            if (!easing_xy.is_array() || easing_xy.size() != 2)
                throw std::runtime_error("easing_xy requires exactly two easing names");
            key.easing_xy = std::array<std::string, 2>{
                easing_xy[0].get<std::string>(), easing_xy[1].get<std::string>()};
            static const std::array<std::string_view, 22> axis_easings{
                "linear", "in_quad", "out_quad", "in_out_quad", "in_cubic",
                "out_cubic", "in_out_cubic", "in_expo", "out_expo", "in_out_expo",
                "in_sine", "out_sine", "in_out_sine", "in_back", "out_back",
                "in_out_back", "in_elastic", "out_elastic", "in_out_elastic",
                "in_bounce", "out_bounce", "in_out_bounce"};
            for (const auto& easing : *key.easing_xy)
                if (std::find(axis_easings.begin(), axis_easings.end(), easing) == axis_easings.end() &&
                    easing != "smoothstep" && easing != "hold")
                    throw std::runtime_error("unsupported per-axis animation easing: " + easing);
            if (track.property != "position" || key.value.size() != 2 || key.easing ||
                key.interpolation || key.bezier_curve || key.temporal_in_dx ||
                key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy ||
                key.spring || track.spring)
                throw std::runtime_error("easing_xy requires a 2D position keyframe without other timing overrides");
        }
        if (key_value.contains("spring")) key.spring = decode_spring(key_value.at("spring"), "animation keyframe spring");
        if (key_value.contains("bezier_curve"))
            key.bezier_curve = decode_bezier_curve(key_value, "bezier", "animation keyframe");
        if (key_value.contains("roving") && !key_value.at("roving").is_boolean())
            throw std::runtime_error("roving must be a boolean");
        key.roving = key_value.value("roving", false);
        if (key_value.contains("temporal_in_dx")) key.temporal_in_dx = key_value.at("temporal_in_dx").get<float>();
        if (key_value.contains("temporal_in_dy")) key.temporal_in_dy = key_value.at("temporal_in_dy").get<float>();
        if (key_value.contains("temporal_out_dx")) key.temporal_out_dx = key_value.at("temporal_out_dx").get<float>();
        if (key_value.contains("temporal_out_dy")) key.temporal_out_dy = key_value.at("temporal_out_dy").get<float>();
        for (const auto handle : {key.temporal_in_dx, key.temporal_in_dy,
                                  key.temporal_out_dx, key.temporal_out_dy})
            if (handle && !std::isfinite(*handle))
                throw std::runtime_error("temporal bezier handles must be finite");
        if (key.temporal_in_dx.has_value() != key.temporal_in_dy.has_value() ||
            key.temporal_out_dx.has_value() != key.temporal_out_dy.has_value())
            throw std::runtime_error("temporal handle dx and dy must be provided together");
        if (key.temporal_in_dx && *key.temporal_in_dx > 0.0f)
            throw std::runtime_error("temporal_in_dx must be non-positive");
        if (key.temporal_out_dx && *key.temporal_out_dx < 0.0f)
            throw std::runtime_error("temporal_out_dx must be non-negative");
        if (key.easing && *key.easing != "linear" && *key.easing != "in_quad" &&
            *key.easing != "out_quad" && *key.easing != "in_out_quad" &&
            *key.easing != "in_cubic" && *key.easing != "out_cubic" &&
            *key.easing != "in_out_cubic" && *key.easing != "in_expo" &&
            *key.easing != "out_expo" && *key.easing != "in_out_expo" &&
            *key.easing != "in_sine" && *key.easing != "out_sine" &&
            *key.easing != "in_out_sine" && *key.easing != "in_back" &&
            *key.easing != "out_back" && *key.easing != "in_out_back" &&
            *key.easing != "in_elastic" && *key.easing != "out_elastic" &&
            *key.easing != "in_out_elastic" && *key.easing != "in_bounce" &&
            *key.easing != "out_bounce" && *key.easing != "in_out_bounce" &&
            *key.easing != "smoothstep" && *key.easing != "hold" &&
            *key.easing != "bezier" && *key.easing != "spring")
            throw std::runtime_error("unsupported per-key animation easing: " + *key.easing);
        if (key.spring && key.easing != "spring" && track.easing != "spring")
            throw std::runtime_error("per-key spring config requires easing='spring'");
        if (key.interpolation && *key.interpolation != "linear" &&
            *key.interpolation != "hold" && *key.interpolation != "bezier" &&
            *key.interpolation != "auto_bezier")
            throw std::runtime_error("keyframe interpolation must be linear, hold, bezier, or auto_bezier");
        if (key.interpolation == "auto_bezier" &&
            (key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
            throw std::runtime_error("auto_bezier cannot be combined with authored Bezier data");
        if (key.interpolation == "bezier" && key.bezier_curve &&
            (key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy))
            throw std::runtime_error("bezier_curve and temporal handles cannot be combined on one keyframe");
        if ((key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy) &&
            (key.interpolation != "bezier" || key.bezier_curve))
            throw std::runtime_error("temporal handles require interpolation='bezier' without bezier_curve");
        const bool has_temporal_handles = key.temporal_in_dx || key.temporal_in_dy ||
            key.temporal_out_dx || key.temporal_out_dy;

        if (key.roving && (has_temporal_handles || key.bezier_curve))
            throw std::runtime_error("roving keyframes cannot contain authored Bezier handles");
        if (key.interpolation && key.easing)
            throw std::runtime_error("keyframe interpolation cannot be combined with easing");
        if (key.interpolation == "bezier" && !key.bezier_curve && !has_temporal_handles)
            throw std::runtime_error("keyframe interpolation='bezier' requires bezier_curve or temporal handles");
        if (key.interpolation && *key.interpolation != "bezier" && key.bezier_curve)
            throw std::runtime_error("bezier_curve requires interpolation='bezier'");
        if (!key.interpolation && ((key.easing == "bezier") != key.bezier_curve.has_value()))
            throw std::runtime_error("per-key easing='bezier' and bezier_curve must be provided together");
        if (key.interpolation && key.easing)
            throw std::runtime_error("keyframe interpolation cannot be combined with easing");
        if (key.roving && (key.interpolation == "bezier" || key.interpolation == "auto_bezier" ||
                           has_temporal_handles || key.bezier_curve))
            throw std::runtime_error("roving cannot be combined with authored or automatic Bezier timing");
        if (key.interpolation == "auto_bezier" &&
            (key.easing || key.bezier_curve || has_temporal_handles))
            throw std::runtime_error("auto_bezier cannot be combined with easing or authored handles");
        track.keyframes.push_back(std::move(key));
    }
    if (track.keyframes.empty()) {
        if (!track.data_feed && !track.audio_react)
            throw std::runtime_error("animation track requires keyframes or a procedural data driver");
        track.keyframes.push_back({Frame{0}, {0.0f}});
    }
    if (track.easing == "spring" && track.keyframes.size() < 2)
        throw std::runtime_error("spring segment requires positive frame span");
    for (std::size_t index = 0; index + 1 < track.keyframes.size(); ++index)
        if ((track.keyframes[index].easing == "spring" ||
             (!track.keyframes[index].easing && track.easing == "spring")) &&
            track.keyframes[index].frame >= track.keyframes[index + 1].frame)
            throw std::runtime_error("spring segment requires positive frame span");
    if (!track.keyframes.empty() &&
        (track.keyframes.back().easing == "spring" || track.keyframes.back().spring))
        throw std::runtime_error("spring segment requires positive frame span");
    for (std::size_t index = 1; index < track.keyframes.size(); ++index)
        if (track.keyframes[index - 1].frame == track.keyframes[index].frame)
            throw std::runtime_error("animation keyframe frames must be unique");
    for (std::size_t index = 0; index + 1 < track.keyframes.size(); ++index) {
        const auto& key = track.keyframes[index];
        const bool spring_segment = key.easing == "spring" ||
            (!key.easing && track.easing == "spring");
        if (spring_segment && !key.spring && !track.spring)
            throw std::runtime_error("spring segment requires a config on its keyframe or track");
    }
    return track;
}


}  // namespace

AnimationTrackPlan decode_animation_track(const nlohmann::json& value) {
    return decode_animation_track_impl(value);
}

std::optional<std::array<float, 4>> decode_bezier_curve(
    const nlohmann::json& object, std::string_view easing, std::string_view field) {
    const bool has_curve = object.contains("bezier_curve");
    if ((easing == "bezier") != has_curve)
        throw std::runtime_error(std::string(field) +
            " requires easing='bezier' and bezier_curve together");
    if (!has_curve) return std::nullopt;
    const auto& raw = object.at("bezier_curve");
    if (!raw.is_array() || raw.size() != 4)
        throw std::runtime_error(std::string(field) + ".bezier_curve must contain [x1, y1, x2, y2]");
    std::array<float, 4> curve{};
    for (std::size_t i = 0; i < curve.size(); ++i) {
        curve[i] = raw.at(i).get<float>();
        if (!std::isfinite(curve[i]))
            throw std::runtime_error(std::string(field) + ".bezier_curve values must be finite");
    }
    if (curve[0] < 0.0f || curve[0] > 1.0f || curve[2] < 0.0f || curve[2] > 1.0f ||
        curve[1] < -10.0f || curve[1] > 10.0f || curve[3] < -10.0f || curve[3] > 10.0f)
        throw std::runtime_error(std::string(field) +
            ".bezier_curve x handles must be in [0, 1] and y handles in [-10, 10]");
    return curve;
}


TransitionPlan decode_transition(const nlohmann::json& value) {
    TransitionPlan plan;
    plan.id = value.value("id", std::string{});
    plan.kind = value.at("kind").get<std::string>();
    plan.duration_frames = Frame{value.at("duration_frames").get<std::int64_t>()};
    plan.delay_frames = Frame{value.value("delay_frames", std::int64_t{0})};
    plan.easing = value.value("easing", std::string{"linear"});
    plan.direction = value.value("direction", std::string{});
    if (!value.contains("params")) return plan;
    const auto& params = value.at("params");
    const std::vector<std::string> allowed = [&]() {
        if (plan.kind == "slide") return std::vector<std::string>{"distance"};
        if (plan.kind == "smooth_wipe") return std::vector<std::string>{"feather"};
        if (plan.kind == "circle_iris") return std::vector<std::string>{"feather", "center"};
        if (plan.kind == "flash") return std::vector<std::string>{"color"};
        if (plan.kind == "procedural_remotion")
            return std::vector<std::string>{"seed", "inner_color", "mid_color", "outer_color"};
        if (plan.kind == "remotion")
            return std::vector<std::string>{"speed", "remotion_direction", "angle"};
        return std::vector<std::string>{};
    }();
    for (auto it = params.begin(); it != params.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            throw std::runtime_error("parameter '" + it.key() + "' is not supported by transition '" + plan.kind + "'");
    plan.distance = params.value("distance", plan.distance);
    plan.feather = params.value("feather", plan.feather);
    if (params.contains("center"))
        for (std::size_t i = 0; i < 2; ++i) plan.center[i] = params.at("center").at(i).get<float>();
    const auto read_color = [&](const char* key, std::array<float, 4>& target) {
        if (!params.contains(key)) return;
        const auto& raw = params.at(key);
        if (raw.is_string()) {
            const auto parsed = parse_hex_color(raw.get<std::string>());
            if (!parsed) throw std::runtime_error(std::string(key) + " must be a #RRGGBB hex or RGBA array");
            target = {parsed->r, parsed->g, parsed->b, parsed->a};
            return;
        }
        if (!raw.is_array() || raw.size() != 4)
            throw std::runtime_error(std::string(key) + " must be a #RRGGBB hex or RGBA array");
        for (std::size_t i = 0; i < 4; ++i) target[i] = raw.at(i).get<float>();
    };
    read_color("color", plan.color);
    read_color("inner_color", plan.inner_color);
    read_color("mid_color", plan.mid_color);
    read_color("outer_color", plan.outer_color);
    plan.seed = params.value("seed", plan.seed);
    plan.speed = params.value("speed", plan.speed);
    plan.remotion_direction = params.value("remotion_direction", plan.remotion_direction);
    plan.angle = params.value("angle", plan.angle);
    if (!std::isfinite(plan.distance) || plan.distance < 0.0f || plan.distance > 4.0f ||
        !std::isfinite(plan.feather) || plan.feather < 0.0f || plan.feather > 1.0f ||
        !std::isfinite(plan.seed) || !std::isfinite(plan.speed) || plan.speed <= 0.0f ||
        !std::isfinite(plan.remotion_direction) || !std::isfinite(plan.angle))
        throw std::runtime_error("transition parameters are outside their finite supported ranges");
    for (const auto value : plan.center)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            throw std::runtime_error("transition center components must be finite in [0, 1]");
    for (const auto* color : {&plan.color, &plan.inner_color, &plan.mid_color, &plan.outer_color})
        for (const auto value : *color)
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                throw std::runtime_error("transition color components must be finite in [0, 1]");
    return plan;
}


}  // namespace chronon3d::render_plan::detail
