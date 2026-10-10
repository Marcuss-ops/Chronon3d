#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/animation_property_registry.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <algorithm>
#include <limits>
#include <array>
#include <cstdint>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace chronon3d::render_plan::detail {
namespace {

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

}  // namespace

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    if (value.contains("follow_path")) {
        const auto& raw = value.at("follow_path");
        LayerPlan::FollowPathDriverPlan driver;
        if (raw.contains("path_id")) driver.path_id = raw.at("path_id").get<std::string>();
        if (raw.contains("path_commands"))
            driver.path_commands = decode_follow_path_commands(raw.at("path_commands"));
        driver.progress = decode_animation_track(raw.at("progress"));
        driver.progress.property = "follow_path_progress";
        for (const auto& key : driver.progress.keyframes) {
            if (key.value.size() != 1 || !std::isfinite(key.value.front()))
                throw std::runtime_error("follow_path progress values must be finite scalars");
        }
        driver.auto_orient = raw.value("auto_orient", false);
        driver.offset_deg = raw.value("offset_deg", 0.0f);
        driver.clamp_end = raw.value("clamp_end", true);
        if (!std::isfinite(driver.offset_deg) || std::abs(driver.offset_deg) > 1000000.0f)
            throw std::runtime_error("follow_path offset_deg is outside supported bounds");
        layer.follow_path = std::move(driver);
    }
    decode_layer_layout(layer, value);
    if (value.contains("transition_in")) layer.transition_in = decode_transition(value.at("transition_in"));
    if (value.contains("transition_out")) layer.transition_out = decode_transition(value.at("transition_out"));
    if (value.contains("light")) {
        if (layer.type != LayerType::Light)
            throw std::runtime_error("light is only valid for light layers");
        const auto& raw = value.at("light");
        LightLayerPlan light;
        light.radius = raw.value("radius", light.radius);
        light.intensity = raw.value("intensity", light.intensity);
        const auto color = parse_hex_color(raw.value("color", std::string{"#FFFFFF"}));
        if (!color) throw std::runtime_error("light.color must be a #RRGGBB or #RRGGBBAA color");
        light.color = {color->r, color->g, color->b, color->a};
        if (!std::isfinite(light.radius) || light.radius <= 0.0f ||
            !std::isfinite(light.intensity) || light.intensity < 0.0f || light.intensity > 16.0f)
            throw std::runtime_error("light radius must be positive and intensity must be in [0, 16]");
        if (raw.contains("kind")) {
            const auto kind_name = raw.at("kind").get<std::string>();
            if (kind_name == "radial") light.kind = LightKindPlan::Radial;
            else if (kind_name == "point") light.kind = LightKindPlan::Point;
            else if (kind_name == "spot") light.kind = LightKindPlan::Spot;
            else if (kind_name == "directional") light.kind = LightKindPlan::Directional;
            else throw std::runtime_error("light.kind must be radial, point, spot, or directional");
        }
        if (raw.contains("direction")) {
            const auto& direction = raw.at("direction");
            if (!direction.is_array() || direction.size() != 3)
                throw std::runtime_error("light.direction must contain three numbers");
            for (std::size_t i = 0; i < 3; ++i) light.direction[i] = direction.at(i).get<float>();
        }
        light.range = raw.value("range", light.range);
        light.decay = raw.value("decay", light.decay);
        light.angle = raw.value("angle", light.angle);
        light.penumbra = raw.value("penumbra", light.penumbra);
        light.cast_shadow = raw.value("cast_shadow", light.cast_shadow);
        light.shadow_radius = raw.value("shadow_radius", light.shadow_radius);
        light.shadow_bias = raw.value("shadow_bias", light.shadow_bias);
        const bool direction_finite = std::all_of(light.direction.begin(), light.direction.end(),
            [](float component) { return std::isfinite(component); });
        const float direction_len2 = light.direction[0] * light.direction[0] +
            light.direction[1] * light.direction[1] + light.direction[2] * light.direction[2];
        if (!direction_finite || (light.kind != LightKindPlan::Radial && !(direction_len2 > 0.0f)))
            throw std::runtime_error("light.direction must be finite and non-zero for spot and directional lights");
        if (!std::isfinite(light.range) || light.range < 0.0f || light.range > 1000000.0f)
            throw std::runtime_error("light.range must be finite and in [0, 1000000]; 0 means unbounded");
        if (!std::isfinite(light.decay) || light.decay < 0.0f || light.decay > 8.0f)
            throw std::runtime_error("light.decay must be finite and in [0, 8]");
        if (!std::isfinite(light.angle) || light.angle <= 0.0f || light.angle > 3.14159265358979323846f)
            throw std::runtime_error("light.angle must be finite and in (0, pi] radians");
        if (!std::isfinite(light.penumbra) || light.penumbra < 0.0f || light.penumbra > 1.0f)
            throw std::runtime_error("light.penumbra must be finite and in [0, 1]");
        if (!std::isfinite(light.shadow_radius) || light.shadow_radius < 0.0f || light.shadow_radius > 512.0f)
            throw std::runtime_error("light.shadow_radius must be finite and in [0, 512]");
        if (!std::isfinite(light.shadow_bias) || light.shadow_bias < 0.0f || light.shadow_bias > 16.0f)
            throw std::runtime_error("light.shadow_bias must be finite and in [0, 16]");
        if (light.cast_shadow && light.kind == LightKindPlan::Radial)
            throw std::runtime_error("light.cast_shadow requires a point, spot, or directional light");
        layer.light = light;
        if (!value.contains("blend_mode")) layer.blend_mode = BlendMode::Screen;
    }
    if (layer.type == LayerType::Light && !layer.light)
        throw std::runtime_error("light layers require a light object");
    if (value.contains("particle_emitter")) {
        if (layer.type != LayerType::Particles)
            throw std::runtime_error("particle_emitter is only valid for particles layers");
        const auto& raw = value.at("particle_emitter");
        ParticleEmitterParams params;
        params.count = raw.at("count").get<u32>();
        params.seed = raw.at("seed").get<u32>();
        const auto read_vec2 = [](const nlohmann::json& object, const char* key) {
            const auto& input = object.at(key);
            if (!input.is_array() || input.size() != 2)
                throw std::runtime_error(std::string("particle_emitter.") + key + " must have two numbers");
            const Vec2 result{input[0].get<float>(), input[1].get<float>()};
            if (!std::isfinite(result.x) || !std::isfinite(result.y))
                throw std::runtime_error(std::string("particle_emitter.") + key + " must be finite");
            return result;
        };
        params.emitter_size = read_vec2(raw, "emitter_size");
        params.velocity_min = read_vec2(raw, "velocity_min");
        params.velocity_max = read_vec2(raw, "velocity_max");
        const auto& lifetime = raw.at("lifetime");
        const auto& size = raw.at("size");
        if (!lifetime.is_array() || lifetime.size() != 2 || !size.is_array() || size.size() != 2)
            throw std::runtime_error("particle_emitter lifetime and size must be [min, max]");
        params.lifetime_min = lifetime[0].get<float>();
        params.lifetime_max = lifetime[1].get<float>();
        params.size_min = size[0].get<float>();
        params.size_max = size[1].get<float>();
        params.gravity = raw.value("gravity", 0.0f);
        params.turbulence = raw.value("turbulence", 0.0f);
        const auto shape_name = raw.value("shape", std::string{"box"});
        if (shape_name == "ellipse") params.shape = EmitterShape::Ellipse;
        else if (shape_name == "edge") params.shape = EmitterShape::Edge;
        else if (shape_name == "path") params.shape = EmitterShape::Path;
        else if (shape_name != "box") throw std::runtime_error("particle emitter shape must be box, ellipse, edge, or path");
        params.path_id = raw.value("path_id", std::string{});
        params.sprite_asset = raw.value("sprite_asset", std::string{});
        if (raw.contains("blend_mode")) {
            const auto mode=blend_mode(raw.at("blend_mode").get<std::string>());
            if (!mode) throw std::runtime_error("particle emitter blend_mode is unsupported");
            params.blend_mode=*mode;
        }
        if (raw.contains("sprite_scale")) {
            const auto& range = raw.at("sprite_scale");
            if (!range.is_array() || range.size()!=2) throw std::runtime_error("particle sprite_scale must be [min, max]");
            params.sprite_scale_min=range[0].get<float>(); params.sprite_scale_max=range[1].get<float>();
        }
        if (raw.contains("rotation")) {
            const auto& range = raw.at("rotation");
            if (!range.is_array() || range.size()!=2) throw std::runtime_error("particle rotation must be [min, max]");
            params.rotation_min=range[0].get<float>(); params.rotation_max=range[1].get<float>();
        }
        if (raw.contains("angular_velocity")) {
            const auto& range = raw.at("angular_velocity");
            if (!range.is_array() || range.size()!=2) throw std::runtime_error("particle angular_velocity must be [min, max]");
            params.angular_velocity_min=range[0].get<float>(); params.angular_velocity_max=range[1].get<float>();
        }
        if (raw.contains("path_points")) {
            for (const auto& point : raw.at("path_points")) {
                if (!point.is_array() || point.size()!=2) throw std::runtime_error("particle path_points entries need two values");
                params.path_points.push_back({point[0].get<float>(),point[1].get<float>()});
            }
        }
        if (raw.contains("color_ramp")) {
            std::vector<graphics::GradientStop> stops;
            for (const auto& stop : raw.at("color_ramp")) {
                const auto& color = stop.at("color");
                if (!color.is_array() || color.size() != 4)
                    throw std::runtime_error("particle color ramp stops require RGBA");
                const float position = stop.at("position").get<float>();
                const Color decoded{color[0].get<float>(), color[1].get<float>(),
                                    color[2].get<float>(), color[3].get<float>()};
                const auto valid = [](float component) {
                    return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
                };
                if (!valid(position) || !valid(decoded.r) || !valid(decoded.g) ||
                    !valid(decoded.b) || !valid(decoded.a))
                    throw std::runtime_error("particle color ramp values must be finite and in [0, 1]");
                stops.push_back({position, decoded});
            }
            if (stops.size() < 2)
                throw std::runtime_error("particle color ramp requires at least two stops");
            params.color_ramp = graphics::GradientDefinition::linear(
                {0.0f, 0.0f}, {1.0f, 0.0f}, std::move(stops));
        }
        if (const auto error = validate_particle_emitter(params); !error.empty())
            throw std::runtime_error(error);
        layer.particle_emitter = std::move(params);
    }
    if (layer.type == LayerType::Particles && !layer.particle_emitter)
        throw std::runtime_error("particles layers require particle_emitter");
    if (value.contains("parent"))
        layer.parent = value.at("parent").get<std::string>();
    if (value.contains("track_matte")) {
        const auto& raw_matte = value.at("track_matte");
        TrackMattePlan matte;
        matte.source = raw_matte.at("source").get<std::string>();
        const auto matte_type = raw_matte.at("type").get<std::string>();
        if (matte_type == "alpha") matte.type = TrackMatteTypePlan::Alpha;
        else if (matte_type == "alpha_inverted") matte.type = TrackMatteTypePlan::AlphaInverted;
        else if (matte_type == "luma") matte.type = TrackMatteTypePlan::Luma;
        else if (matte_type == "luma_inverted") matte.type = TrackMatteTypePlan::LumaInverted;
        else throw std::runtime_error("unsupported track_matte.type: " + matte_type);
        layer.track_matte = std::move(matte);
    }
    if (value.contains("time_remap")) {
        const auto& raw_remap = value.at("time_remap");
        TimeRemapPlan remap;
        remap.speed = optional_value<float>(raw_remap, "speed");
        remap.reverse = raw_remap.value("reverse", false);
        remap.freeze_frame = optional_frame(raw_remap, "freeze_frame");
        if (raw_remap.contains("animation")) {
            AnimationTrackPlan track;
            const auto& animation = raw_remap.at("animation");
            track.property = "time_remap";
            track.easing = animation.value("easing", std::string{"linear"});
            track.bezier_curve = decode_bezier_curve(animation, track.easing, "time_remap.animation");
            for (const auto& key_value : animation.at("keyframes")) {
                AnimationKeyframePlan key;
                key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
                const auto& raw = key_value.at("value");
                if (raw.is_array()) {
                    if (raw.size() != 1)
                        throw std::runtime_error("time_remap keyframes require scalar values");
                    key.value.push_back(raw.at(0).get<float>());
                } else {
                    key.value.push_back(raw.get<float>());
                }
                if (key_value.contains("roving") || key_value.contains("temporal_in_dx") ||
                    key_value.contains("temporal_in_dy") || key_value.contains("temporal_out_dx") ||
                    key_value.contains("temporal_out_dy"))
                    throw std::runtime_error("time_remap keyframes do not support roving or temporal handles");
                if (key_value.contains("interpolation"))
                    key.interpolation = key_value.at("interpolation").get<std::string>();
                if (key_value.contains("easing"))
                    key.easing = key_value.at("easing").get<std::string>();
                if (key_value.contains("bezier_curve"))
                    key.bezier_curve = decode_bezier_curve(key_value, "bezier", "time_remap keyframe");
                if (!std::isfinite(key.value.front()) || key.value.front() < 0.0f ||
                    key.value.front() > 1000000.0f)
                    throw std::runtime_error("time_remap source frame values must be finite and in [0, 1000000]");
                track.keyframes.push_back(std::move(key));
            }
            remap.animation = std::move(track);
        }
        if (remap.speed && (!std::isfinite(*remap.speed) || *remap.speed <= 0.0f || *remap.speed > 1000.0f))
            throw std::runtime_error("time_remap.speed must be finite and in (0, 1000]");
        if (remap.freeze_frame && (remap.freeze_frame->integral() < 0 || remap.freeze_frame->integral() > 1000000))
            throw std::runtime_error("time_remap.freeze_frame must be in [0, 1000000]");
        if (remap.freeze_frame && remap.animation)
            throw std::runtime_error("time_remap cannot specify both freeze_frame and animation");
        layer.time_remap = std::move(remap);
    }
    layer.asset = value.value("asset", std::string{});
    layer.input_color_space = value.value("input_color_space", std::string{"srgb"});
    layer.ocio_input_space = value.value("ocio_input_space", std::string{});
    if (layer.type != LayerType::Image && layer.type != LayerType::Video &&
        value.contains("ocio_input_space"))
        throw std::runtime_error("ocio_input_space is only valid for image or video layers");
    if (layer.type != LayerType::Image && value.contains("input_color_space"))
        throw std::runtime_error("input_color_space is only valid for image layers");
    if (layer.input_color_space != "srgb" && layer.input_color_space != "linearsrgb" &&
        layer.input_color_space != "rec709" && layer.input_color_space != "displayp3")
        throw std::runtime_error(
            "image input_color_space must be srgb, linearsrgb, rec709, or displayp3");
    layer.source = value.value("source", std::string{});
    if (layer.type == LayerType::Mesh) {
        for (const auto* key : {"text", "shape", "particle_emitter", "light", "style", "background_look", "mask", "masks", "text_animators", "spans", "fill_gradient_animation"})
            if (value.contains(key))
                throw std::runtime_error(std::string("mesh layers cannot contain '") + key + "'");
        if (value.contains("animation") && value.at("animation").is_object() &&
            value.at("animation").contains("tracks") && !value.at("animation").at("tracks").empty())
            throw std::runtime_error("mesh layers cannot contain per-vertex animation tracks");
    }
    if (layer.type == LayerType::Mesh && (!layer.asset.empty() || !layer.source.empty()))
        throw std::runtime_error("mesh layers use inline geometry and cannot also carry asset or source references");
    decode_layer_inline_mesh(layer, value);
    layer.text = value.value("text", std::string{});
    if (value.contains("text_counter")) {
        if (layer.type != LayerType::Text)
            throw std::runtime_error("text_counter is only valid for text layers");
        const auto& raw = value.at("text_counter");
        if (!raw.is_object())
            throw std::runtime_error("text_counter must be an object");
        TextCounterPlan plan;
        plan.token = raw.value("token", plan.token);
        if (plan.token.empty() || plan.token.size() > 64 ||
            layer.text.find(plan.token) == std::string::npos ||
            layer.text.find(plan.token, layer.text.find(plan.token) + plan.token.size()) != std::string::npos)
            throw std::runtime_error("text_counter token must occur exactly once in text and contain at most 64 bytes");
        const auto& counter = raw.at("counter");
        if (!counter.is_object())
            throw std::runtime_error("text_counter.counter must be an object");
        auto& target = plan.counter;
        target.from = counter.at("from").get<float>();
        target.to = counter.at("to").get<float>();
        target.start_frame = Frame{counter.value("start_frame", std::int64_t{0})};
        target.duration_frames = Frame{counter.value("duration_frames", std::int64_t{30})};
        target.easing = counter.value("easing", std::string{"out_expo"});
        target.format = counter.value("format", std::string{"plain"});
        target.currency = counter.value("currency", std::string{"USD"});
        target.decimals = counter.value("decimals", 0);
        target.prefix = counter.value("prefix", std::string{});
        target.suffix = counter.value("suffix", std::string{});
        static constexpr std::array<std::string_view, 23> easings{
            "linear", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic",
            "in_out_cubic", "in_expo", "out_expo", "in_out_expo", "in_sine", "out_sine",
            "in_out_sine", "in_back", "out_back", "in_out_back", "in_elastic", "out_elastic",
            "in_out_elastic", "in_bounce", "out_bounce", "in_out_bounce", "smoothstep"};
        if (!std::isfinite(target.from) || !std::isfinite(target.to) ||
            std::abs(target.from) > 1.0e12f || std::abs(target.to) > 1.0e12f ||
            target.start_frame.integral() < 0 || target.start_frame.integral() > 1000000 ||
            target.duration_frames.integral() < 1 || target.duration_frames.integral() > 1000000 ||
            std::find(easings.begin(), easings.end(), target.easing) == easings.end() ||
            (target.format != "plain" && target.format != "currency" &&
             target.format != "percent" && target.format != "compact") ||
            target.decimals < 0 || target.decimals > 6 || target.prefix.size() > 32 ||
            target.suffix.size() > 32 || target.currency.size() > 8)
            throw std::runtime_error("text_counter values are outside supported bounds");
        if (static_cast<std::uint64_t>(target.start_frame.integral()) +
                static_cast<std::uint64_t>(target.duration_frames.integral()) > 1000000U)
            throw std::runtime_error("text_counter animation exceeds the supported frame limit");
        layer.text_counter = std::move(plan);
    }
    decode_layer_text_spans(layer, value);
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

    decode_shape_and_fill_gradient(layer, value);

    decode_layer_masks(layer, value);
    decode_layer_effects(layer, value);
    decode_background_look(layer, value);
    if (value.contains("backdrop_filter")) {
        const auto& raw_bf = value.at("backdrop_filter");
        BackdropFilterPlan filter;
        filter.blur = raw_bf.value("blur", filter.blur);
        filter.brightness = raw_bf.value("brightness", filter.brightness);
        filter.saturation = raw_bf.value("saturation", filter.saturation);
        filter.refraction_strength = raw_bf.value("refraction_strength", filter.refraction_strength);
        if (raw_bf.contains("refraction_field"))
            filter.refraction_field = decode_field2d_plan(raw_bf.at("refraction_field"));
        filter.rim_strength = raw_bf.value("rim_strength", filter.rim_strength);
        filter.rim_width = raw_bf.value("rim_width", filter.rim_width);
        filter.chromatic_dispersion = raw_bf.value("chromatic_dispersion", filter.chromatic_dispersion);
        filter.grain = raw_bf.value("grain", filter.grain);
        filter.opacity = raw_bf.value("opacity", filter.opacity);
        filter.validate();
        layer.backdrop_filter = filter;
    }
    decode_effect_parameter_tracks(layer, value);
    if (layer.type == LayerType::Adjustment) {
        if (layer.effects.empty())
            throw std::runtime_error("adjustment layers require at least one effect");
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() != "id" && it.key() != "type" && it.key() != "effects" &&
                it.key() != "effect_param_tracks" && it.key() != "background_look")
                throw std::runtime_error(
                    "adjustment layers support only 'id', 'type', and a non-empty 'effects' stack; found '" +
                    it.key() + "'");
        }
        layer.type = LayerType::Adjustment;
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
    layer.skew_angle_deg = value.value("skew", 0.0f);
    layer.skew_axis_deg = value.value("skew_axis", 0.0f);
    if (!std::isfinite(layer.skew_angle_deg) || std::abs(layer.skew_angle_deg) > 80.0f ||
        !std::isfinite(layer.skew_axis_deg) || std::abs(layer.skew_axis_deg) > 1000000.0f)
        throw std::runtime_error("layer skew and skew_axis are outside supported bounds");
    layer.enable_3d = value.value("enable_3d", false);
    if (value.contains("casts_shadows"))
        layer.casts_shadows = value.at("casts_shadows").get<bool>();
    if (value.contains("accepts_shadows"))
        layer.accepts_shadows = value.at("accepts_shadows").get<bool>();
    layer.screen_space = value.value("screen_space", false);
    if (value.contains("safe_insets")) {
        const auto& insets = value.at("safe_insets");
        if (!insets.is_object())
            throw std::runtime_error("layer.safe_insets must be an object");
        std::array<float, 4> parsed{
            insets.value("top", 0.0f), insets.value("right", 0.0f),
            insets.value("bottom", 0.0f), insets.value("left", 0.0f)};
        for (const float inset : parsed)
            if (!std::isfinite(inset) || inset < 0.0f)
                throw std::runtime_error("layer.safe_insets values must be finite and non-negative");
        layer.safe_insets = parsed;
    }
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
    if (value.contains("crop")) {
        if (layer.type != LayerType::Image)
            throw std::runtime_error("crop is only valid for image layers");
        const auto& raw_crop = value.at("crop");
        ImageCropPlan crop;
        crop.enabled = raw_crop.value("enabled", true);
        if (raw_crop.contains("origin")) crop.origin = raw_crop.at("origin").get<std::array<float, 2>>();
        if (raw_crop.contains("size")) crop.size = raw_crop.at("size").get<std::array<float, 2>>();
        crop.flip_x = raw_crop.value("flip_x", false);
        for (std::size_t i = 0; i < 2; ++i) {
            const float origin = crop.origin[i];
            const float extent = crop.size[i];
            if (!std::isfinite(origin) || !std::isfinite(extent) || origin < 0.0f ||
                extent <= 0.0f || origin + extent > 1.0f)
                throw std::runtime_error("crop origin and size must define a finite normalized rectangle");
        }
        layer.crop = crop;
    }
    layer.radius = optional_value<float>(value, "radius");

    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font = style.value("font", std::string{});
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        style_plan.text_fit_mode = style.value("fit_mode", std::string{"none"});
        style_plan.min_font_size = optional_value<float>(style, "min_font_size");
        style_plan.max_font_size = optional_value<float>(style, "max_font_size");
        if (style_plan.text_fit_mode != "none" &&
            style_plan.text_fit_mode != "shrink_only" &&
            style_plan.text_fit_mode != "grow_and_shrink")
            throw std::runtime_error("text style fit_mode must be none, shrink_only, or grow_and_shrink");
        if ((style_plan.min_font_size &&
             (!std::isfinite(*style_plan.min_font_size) || *style_plan.min_font_size <= 0.0f)) ||
            (style_plan.max_font_size &&
             (!std::isfinite(*style_plan.max_font_size) || *style_plan.max_font_size <= 0.0f)) ||
            style_plan.max_font_size.value_or(160.0f) <
                style_plan.min_font_size.value_or(12.0f))
            throw std::runtime_error("text style font size limits must be positive, finite, and ordered");
        if (style_plan.text_fit_mode != "none" && layer.type != LayerType::Text)
            throw std::runtime_error("style.fit_mode is only valid for text layers");
        if ((style_plan.min_font_size || style_plan.max_font_size) &&
            style_plan.text_fit_mode == "none")
            throw std::runtime_error("text font size limits require a non-none fit_mode");
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{stroke.value("color", std::string{}),
                                            optional_value<float>(stroke, "width"),
                                            std::nullopt, std::nullopt, std::nullopt};
        }
        if (style.contains("shadow") && style.contains("shadows"))
            throw std::runtime_error("style.shadow and style.shadows are mutually exclusive");
        const auto decode_shadow = [](const nlohmann::json& shadow) {
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
            return shadow_plan;
        };
        if (style.contains("shadow")) {
            style_plan.shadow = decode_shadow(style.at("shadow"));
        }
        if (style.contains("shadows")) {
            const auto& shadows = style.at("shadows");
            if (!shadows.is_array() || shadows.size() > 3)
                throw std::runtime_error("style.shadows must contain at most 3 shadows");
            for (const auto& raw_shadow : shadows) {
                auto shadow = decode_shadow(raw_shadow);
                const float opacity = shadow.opacity.value_or(1.0f);
                const float blur = shadow.blur.value_or(0.0f);
                if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
                    !std::isfinite(blur) || blur < 0.0f ||
                    !std::isfinite(shadow.offset[0]) || !std::isfinite(shadow.offset[1]) ||
                    shadow.offset_dimensions != 2 || shadow.color.empty())
                    throw std::runtime_error("style.shadows entries require color, finite offset, opacity in [0,1], and non-negative blur");
                style_plan.shadows.push_back(std::move(shadow));
            }
        }
        if (style.contains("backdrop")) {
            if (layer.type != LayerType::Shape)
                throw std::runtime_error("style.backdrop is only valid for shape layers");
            const auto& raw_backdrop = style.at("backdrop");
            BackdropBlurStyle backdrop;
            backdrop.blur = raw_backdrop.at("blur").get<float>();
            backdrop.brightness = raw_backdrop.value("brightness", 1.0f);
            backdrop.saturation = raw_backdrop.value("saturation", 1.0f);
            backdrop.rim_light = raw_backdrop.value("rim_light", 0.0f);
            backdrop.refraction = raw_backdrop.value("refraction", 0.0f);
            backdrop.surface_noise = raw_backdrop.value("surface_noise", 0.0f);
            if (!std::isfinite(backdrop.blur) || backdrop.blur < 0.0f || backdrop.blur > 64.0f ||
                !std::isfinite(backdrop.brightness) || backdrop.brightness < 0.0f || backdrop.brightness > 4.0f ||
                !std::isfinite(backdrop.saturation) || backdrop.saturation < 0.0f || backdrop.saturation > 4.0f ||
                !std::isfinite(backdrop.rim_light) || backdrop.rim_light < 0.0f || backdrop.rim_light > 2.0f ||
                !std::isfinite(backdrop.refraction) || backdrop.refraction < 0.0f || backdrop.refraction > 4.0f ||
                !std::isfinite(backdrop.surface_noise) || backdrop.surface_noise < 0.0f || backdrop.surface_noise > 0.1f)
                throw std::runtime_error("style.backdrop controls are non-finite or outside supported bounds");
            style_plan.backdrop = backdrop;
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
        for (const auto& track_value : value.at("animation").at("tracks")) {
            auto track = decode_animation_track(track_value);
            // Fail closed on unregistered property names: a track the decode
            // pipeline does not know must never survive into a compiled plan
            // where the compiler's property dispatch would silently drop it.
            if (!render_plan::find_animation_property(track.property)) {
                std::string message =
                    "layers[].animation track uses an unregistered property: " +
                    track.property;
                if (const auto nearest =
                        render_plan::nearest_animation_property_name(track.property);
                    !nearest.empty())
                    message += " (did you mean '" + std::string{nearest} + "?')";
                throw std::runtime_error(message);
            }
            animation.tracks.push_back(std::move(track));
        }
        layer.animation = std::move(animation);
    }
    layer.auto_orient = value.value("auto_orient", false);
    if (layer.auto_orient) {
        bool has_position = false;
        bool has_position_span = false;
        bool has_rotation = false;
        if (layer.animation) {
            for (const auto& track : layer.animation->tracks) {
                has_position |= track.property == "position" || track.property == "position_x" ||
                    track.property == "position_y" || track.property == "position_z";
                has_rotation |= track.property == "rotation" || track.property == "rotation_x" ||
                    track.property == "rotation_y" || track.property == "rotation_z";
            }
        }
        if (!has_position)
            throw std::runtime_error("auto_orient requires an animated position track");
        if (has_rotation)
            throw std::runtime_error("auto_orient cannot be combined with explicit rotation tracks");
        for (const auto& track : layer.animation->tracks)
            if (track.property == "position" || track.property == "position_x" ||
                track.property == "position_y" || track.property == "position_z") {
                if (track.keyframes.size() >= 2 &&
                    track.keyframes.back().frame.integral() > track.keyframes.front().frame.integral())
                    has_position_span = true;
                if (track.keyframes.size() >= 2 &&
                    track.keyframes.back().frame.integral() - track.keyframes.front().frame.integral() > 65535)
                    throw std::runtime_error("auto_orient position track span exceeds 65535 frames");
            }
        if (!has_position_span)
            throw std::runtime_error("auto_orient requires a position track spanning at least two frames");
    }
    if (layer.animation) {
        std::unordered_set<std::string> light_track_properties;
        for (const auto& track : layer.animation->tracks) {
            if (track.property != "light_intensity" && track.property != "light_color") continue;
            if (layer.type != LayerType::Light || !layer.light)
                throw std::runtime_error(track.property + " animation requires a light layer");
            if (!light_track_properties.insert(track.property).second)
                throw std::runtime_error("layer has duplicate " + track.property + " animation tracks");
            for (const auto& key : track.keyframes) {
                if (track.property == "light_intensity") {
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < 0.0f || key.value.front() > 16.0f)
                        throw std::runtime_error("light_intensity keyframes must be finite scalars in [0, 16]");
                } else if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                        [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        })) {
                    throw std::runtime_error("light_color keyframes must be finite RGBA values in [0, 1]");
                }
            }
        }
        for (auto& track : layer.animation->tracks) {
            if (track.property != "fill_color") continue;
            std::stable_sort(track.keyframes.begin(), track.keyframes.end(),
                [](const auto& left, const auto& right) {
                    return left.frame < right.frame;
                });
        }
        const AnimationTrackPlan* fill_color_track = nullptr;
        const AnimationTrackPlan* stroke_width_track = nullptr;
        const AnimationTrackPlan* stroke_color_track = nullptr;
        std::vector<std::string> transform_scalar_tracks;
        std::vector<std::string> shadow_track_properties;
        for (const auto& track : layer.animation->tracks) {
            if (track.property == "stroke_dash_offset") {
                if (!layer.shape || !layer.shape->stroke)
                    throw std::runtime_error("stroke_dash_offset animation requires a shape with a stroke");
                for (const auto& key : track.keyframes)
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()))
                        throw std::runtime_error("stroke_dash_offset keyframes must be finite scalar values");
            } else if (track.property == "skew" || track.property == "skew_axis") {
                if (std::find(transform_scalar_tracks.begin(), transform_scalar_tracks.end(),
                              track.property) != transform_scalar_tracks.end())
                    throw std::runtime_error("layer has duplicate " + track.property + " animation tracks");
                transform_scalar_tracks.push_back(track.property);
                std::vector<std::int64_t> frames;
                for (const auto& key : track.keyframes) {
                    const float low = track.property == "skew" ? -80.0f : -1000000.0f;
                    const float high = track.property == "skew" ? 80.0f : 1000000.0f;
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < low || key.value.front() > high ||
                        key.frame.integral() < 0 || key.frame.integral() > 1000000)
                        throw std::runtime_error(track.property +
                            " animation requires bounded frames and scalar values");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error(track.property + " keyframe frames must be unique");
            } else if (track.property == "stroke_width") {
                if (!layer.shape || !layer.shape->stroke)
                    throw std::runtime_error("stroke_width animation requires a shape with a stroke");
                if (stroke_width_track)
                    throw std::runtime_error("layer has duplicate stroke_width animation tracks");
                stroke_width_track = &track;
                std::vector<std::int64_t> frames;
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < 0.0f || key.frame.integral() < 0 ||
                        key.frame.integral() > 1000000)
                        throw std::runtime_error("stroke_width keyframes require bounded frames and finite non-negative scalar values");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("stroke_width keyframe frames must be unique");
            } else if (track.property == "stroke_color") {
                if (!layer.shape || !layer.shape->stroke ||
                    layer.shape->stroke->gradient)
                    throw std::runtime_error("stroke_color animation requires a solid stroke on a shape");
                if (stroke_color_track)
                    throw std::runtime_error("layer has duplicate stroke_color animation tracks");
                stroke_color_track = &track;
                std::vector<std::int64_t> frames;
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 4 || key.frame.integral() < 0 ||
                        key.frame.integral() > 1000000 ||
                        std::any_of(key.value.begin(), key.value.end(), [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        }))
                        throw std::runtime_error("stroke_color keyframes require bounded frames and RGBA values in [0, 1]");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("stroke_color keyframe frames must be unique");
            } else if (track.property.rfind("shadow_", 0) == 0) {
                if (layer.type != LayerType::Shape || !layer.style)
                    throw std::runtime_error("shadow animation requires a shape layer with style shadows");
                const std::size_t shadow_count = layer.style->shadow ? 1 : layer.style->shadows.size();
                std::string property_suffix;
                std::size_t index = 0;
                bool recognized = false;
                for (std::size_t candidate = 0; candidate < 3 && !recognized; ++candidate) {
                    for (const auto suffix : {"_opacity", "_blur", "_offset_x", "_offset_y", "_color"}) {
                        const std::string prefix = "shadow_" + std::to_string(candidate) + suffix;
                        if (track.property == prefix) {
                            index = candidate;
                            property_suffix = suffix;
                            recognized = true;
                            break;
                        }
                    }
                }
                if (!recognized)
                    throw std::runtime_error("unsupported shadow animation property: " + track.property);
                if (index >= shadow_count)
                    throw std::runtime_error("shadow animation refers to an absent shadow style");
                if (std::find(shadow_track_properties.begin(), shadow_track_properties.end(),
                              track.property) != shadow_track_properties.end())
                    throw std::runtime_error("layer has duplicate shadow animation tracks");
                shadow_track_properties.push_back(track.property);
                std::vector<std::int64_t> frames;
                const bool color_track = property_suffix == "_color";
                for (const auto& key : track.keyframes) {
                    bool valid_value = false;
                    if (color_track) {
                        valid_value = key.value.size() == 4 &&
                            std::all_of(key.value.begin(), key.value.end(), [](float component) {
                                return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
                            });
                    } else if (key.value.size() == 1 && std::isfinite(key.value.front())) {
                        const float v = key.value.front();
                        valid_value = property_suffix == "_opacity" ? (v >= 0.0f && v <= 1.0f)
                            : (property_suffix == "_blur" ? v >= 0.0f : true);
                    }
                    if (!valid_value || key.frame.integral() < 0 || key.frame.integral() > 1000000)
                        throw std::runtime_error("shadow animation keyframes are outside supported bounds");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("shadow animation keyframe frames must be unique");
            } else if (track.property == "fill_color") {
                if (fill_color_track)
                    throw std::runtime_error("layer has duplicate fill_color animation tracks");
                fill_color_track = &track;
                if (layer.fill_gradient_animation)
                    throw std::runtime_error(
                        "fill_color and fill_gradient_animation tracks are mutually exclusive");
                if (layer.type == LayerType::Color) {
                    // Color layers use this as their solid paint.
                } else if (layer.type == LayerType::Shape && layer.shape &&
                           (layer.shape->type == ShapeContentTypePlan::Rect ||
                            layer.shape->type == ShapeContentTypePlan::RoundedRect ||
                            layer.shape->type == ShapeContentTypePlan::Ellipse ||
                            layer.shape->type == ShapeContentTypePlan::Star ||
                            layer.shape->type == ShapeContentTypePlan::Polygon ||
                            layer.shape->type == ShapeContentTypePlan::Arrow ||
                            layer.shape->type == ShapeContentTypePlan::DotGrid) &&
                           layer.shape->fill_color && !layer.shape->gradient) {
                    // Only explicit solid fills are animatable; gradients use
                    // the typed fill_gradient_animation track instead.
                } else {
                    throw std::runtime_error(
                        "fill_color animation requires a color layer or a solid filled closed shape");
                }
                std::vector<std::int64_t> frames;
                frames.reserve(track.keyframes.size());
                for (const auto& key : track.keyframes) {
                    if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                        key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                        [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        }))
                        throw std::runtime_error("fill_color keyframes require bounded frames and RGBA values in [0, 1]");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("fill_color keyframe frames must be unique");
            }
        }
    }
    if (value.contains("motion_blur")) {
        const auto& raw = value.at("motion_blur");
        MotionBlurPlan motion_blur;
        motion_blur.shutter_angle = raw.value("shutter_angle", 180.0f);
        motion_blur.samples = raw.value("samples", std::uint32_t{12});
        if (!std::isfinite(motion_blur.shutter_angle) ||
            motion_blur.shutter_angle <= 0.0f || motion_blur.shutter_angle > 360.0f ||
            motion_blur.samples == 0 || motion_blur.samples > 64)
            throw std::runtime_error("motion_blur requires shutter_angle in (0, 360] and samples in [1, 64]");
        if (!layer.animation)
            throw std::runtime_error("motion_blur requires an animated layer position track");
        bool has_position = false;
        for (const auto& track : layer.animation->tracks) {
            has_position = has_position || track.property == "position" ||
                track.property == "position_x" || track.property == "position_y" ||
                track.property == "position_z";
            if (track.property == "scale" || track.property.starts_with("scale_") ||
                track.property == "rotation" || track.property.starts_with("rotation_"))
                throw std::runtime_error("motion_blur currently requires translational motion; scale/rotation tracks need temporal accumulation");
        }
        if (!has_position)
            throw std::runtime_error("motion_blur requires an animated layer position track");
        layer.motion_blur = motion_blur;
    }
    decode_layer_text_animators(layer, value);
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>()))
            layer.blend_mode = mode;
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    return layer;
}

}  // namespace chronon3d::render_plan::detail
