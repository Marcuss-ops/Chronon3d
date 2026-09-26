#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/scene/model/core/transition_catalog_ids.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>

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
#include <filesystem>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
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
    track.bezier_curve = decode_bezier_curve(value, track.easing, "animation track");
    for (const auto& key_value : value.at("keyframes")) {
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
        if (key_value.contains("bezier_curve"))
            key.bezier_curve = decode_bezier_curve(key_value, "bezier", "animation keyframe");
        if (key.interpolation && *key.interpolation != "linear" &&
            *key.interpolation != "hold" && *key.interpolation != "bezier")
            throw std::runtime_error("keyframe interpolation must be linear, hold, or bezier");
        if (key.interpolation && (key.easing || key.bezier_curve))
            throw std::runtime_error("keyframe interpolation cannot be combined with easing or bezier_curve");
        if (key.interpolation == "bezier" && !key.bezier_curve)
            throw std::runtime_error("keyframe interpolation='bezier' requires bezier_curve");
        if (key.interpolation != "bezier" &&
            ((key.easing == "bezier") != key.bezier_curve.has_value()))
            throw std::runtime_error("per-key easing='bezier' and bezier_curve must be provided together");
        track.keyframes.push_back(std::move(key));
    }
    return track;
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
        for (std::size_t i = 0; i < 4; ++i) target[i] = params.at(key).at(i).get<float>();
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

GradientAnimationPlan decode_gradient_animation(const nlohmann::json& value) {
    GradientAnimationPlan animation;
    animation.easing = value.value("easing", std::string{"linear"});
    animation.bezier_curve = decode_bezier_curve(
        value, animation.easing, "fill_gradient_animation");
    for (const auto& key_value : value.at("keyframes")) {
        GradientKeyframePlan key;
        key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
        key.value = decode_gradient_definition(key_value.at("value"));
        animation.keyframes.push_back(std::move(key));
    }
    return animation;
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
    selector.wiggly_frequency = value.value("wiggly_frequency", 1.0f);
    selector.wiggly_amplitude = value.value("wiggly_amplitude", 0.25f);
    selector.wiggly_seed = value.value("wiggly_seed", std::uint64_t{0});
    if (selector.shape == "wiggly" &&
        (!std::isfinite(selector.wiggly_frequency) || selector.wiggly_frequency <= 0.0f ||
         selector.wiggly_frequency > 1000.0f ||
         !std::isfinite(selector.wiggly_amplitude) || selector.wiggly_amplitude < 0.0f ||
         selector.wiggly_amplitude > 1.0f))
        throw std::runtime_error(
            "text selector wiggly_frequency must be in (0, 1000] and wiggly_amplitude must be in [0, 1]");
    if (selector.shape != "wiggly" &&
        (value.contains("wiggly_frequency") || value.contains("wiggly_amplitude") ||
         value.contains("wiggly_seed")))
        throw std::runtime_error("wiggly selector parameters require shape='wiggly'");
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
                if (key_value.contains("interpolation")) key.interpolation = key_value.at("interpolation").get<std::string>();
                if (key_value.contains("easing")) key.easing = key_value.at("easing").get<std::string>();
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
    layer.source = value.value("source", std::string{});
    if (layer.type == LayerType::Mesh) {
        for (const auto* key : {"text", "shape", "particle_emitter", "light", "style", "effects", "mask", "text_animators", "spans", "fill_gradient_animation"})
            if (value.contains(key))
                throw std::runtime_error(std::string("mesh layers cannot contain '") + key + "'");
        if (value.contains("animation") && value.at("animation").is_object() &&
            value.at("animation").contains("tracks") && !value.at("animation").at("tracks").empty())
            throw std::runtime_error("mesh layers cannot contain per-vertex animation tracks");
    }
    if (layer.type == LayerType::Mesh && (!layer.asset.empty() || !layer.source.empty()))
        throw std::runtime_error("mesh layers use inline geometry and cannot also carry asset or source references");
    if (value.contains("inline_mesh")) {
        if (layer.type != LayerType::Mesh)
            throw std::runtime_error("inline_mesh is only valid for mesh layers");
        const auto& raw_mesh = value.at("inline_mesh");
        InlineMeshPlan mesh;
        const auto& vertices = raw_mesh.at("vertices");
        if (!vertices.is_array() || vertices.size() < 3 || vertices.size() > kInlineMeshMaxVertices)
            throw std::runtime_error("inline_mesh.vertices count must be in [3, 250000]");
        mesh.vertices.reserve(vertices.size());
        for (const auto& raw_vertex : vertices) {
            InlineMeshVertex vertex;
            const auto read_components = [](const nlohmann::json& raw, auto& destination,
                                            const char* field, float limit) {
                if (!raw.is_array() || raw.size() != destination.size())
                    throw std::runtime_error(std::string("inline_mesh vertex ") + field +
                                             " has an invalid component count");
                for (std::size_t i = 0; i < destination.size(); ++i) {
                    destination[i] = raw.at(i).get<float>();
                    if (!std::isfinite(destination[i]) || std::abs(destination[i]) > limit)
                        throw std::runtime_error(std::string("inline_mesh vertex ") + field +
                                                 " components are outside the finite contract range");
                }
            };
            read_components(raw_vertex.at("position"), vertex.position, "position", 1'000'000.0f);
            if (raw_vertex.contains("normal"))
                read_components(raw_vertex.at("normal"), vertex.normal, "normal", 1.0f);
            if (raw_vertex.contains("uv"))
                read_components(raw_vertex.at("uv"), vertex.uv, "uv", 1'000'000.0f);
            mesh.vertices.push_back(vertex);
        }
        const auto& indices = raw_mesh.at("indices");
        if (!indices.is_array() || indices.size() < 3 ||
            indices.size() > kInlineMeshMaxIndices || indices.size() % 3 != 0)
            throw std::runtime_error("inline_mesh.indices count must be divisible by three and in [3, 750000]");
        mesh.indices.reserve(indices.size());
        for (const auto& raw_index : indices) {
            if (!raw_index.is_number_unsigned() &&
                (!raw_index.is_number_integer() || raw_index.get<std::int64_t>() < 0))
                throw std::runtime_error("inline_mesh indices must be non-negative integers");
            const auto index = raw_index.get<std::uint64_t>();
            if (index > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("inline_mesh index exceeds uint32 range");
            if (index >= mesh.vertices.size())
                throw std::runtime_error("inline_mesh index is outside the vertex array");
            mesh.indices.push_back(static_cast<std::uint32_t>(index));
        }
        layer.inline_mesh = std::move(mesh);
    } else if (layer.type == LayerType::Mesh) {
        throw std::runtime_error("mesh layers require inline_mesh geometry");
    }
    layer.text = value.value("text", std::string{});
    if (value.contains("spans")) {
        if (layer.type != LayerType::Text)
            throw std::runtime_error("layer.spans is only valid for text layers");
        std::size_t previous_end = 0;
        const auto boundary = [&](std::size_t offset) {
            return offset == 0 || offset == layer.text.size() ||
                (static_cast<unsigned char>(layer.text[offset]) & 0xc0U) != 0x80U;
        };
        for (const auto& raw_span : value.at("spans")) {
            TextSpanOverride span;
            span.byte_start = raw_span.at("start").get<std::size_t>();
            span.byte_end = raw_span.at("end").get<std::size_t>();
            if (span.byte_start < previous_end || span.byte_start >= span.byte_end ||
                span.byte_end > layer.text.size() || !boundary(span.byte_start) || !boundary(span.byte_end))
                throw std::runtime_error("text spans must be sorted, non-overlapping UTF-8 byte ranges within text");
            previous_end = span.byte_end;
            const auto& style = raw_span.at("style");
            if (style.contains("color")) {
                const auto color = parse_hex_color(style.at("color").get<std::string>());
                if (!color) throw std::runtime_error("text span has invalid color");
                span.color = *color;
            }
            if (style.contains("font_size_mult")) {
                const float mult = style.at("font_size_mult").get<float>();
                if (!std::isfinite(mult) || mult <= 0.0f || mult > 100.0f)
                    throw std::runtime_error("text span font_size_mult must be finite and in (0, 100]");
                span.font_size_multiplier = mult;
            }
            if (style.contains("tracking")) span.tracking = style.at("tracking").get<float>();
            if (style.contains("stroke")) {
                const auto& raw_stroke = style.at("stroke");
                TextStrokeStyle stroke;
                if (raw_stroke.contains("color")) {
                    const auto color = parse_hex_color(raw_stroke.at("color").get<std::string>());
                    if (!color) throw std::runtime_error("text span has invalid stroke color");
                    stroke.color = *color;
                }
                if (raw_stroke.contains("width_em"))
                    stroke.width_em = raw_stroke.at("width_em").get<float>();
                if (!std::isfinite(stroke.width_em) || stroke.width_em < 0.0f || stroke.width_em > 100.0f)
                    throw std::runtime_error("text span stroke width_em must be finite and in [0, 100]");
                span.stroke = stroke;
            }
            if (style.contains("highlight")) {
                const auto& raw_highlight = style.at("highlight");
                TextHighlightStyle highlight;
                if (raw_highlight.contains("background")) {
                    const auto color = parse_hex_color(
                        raw_highlight.at("background").get<std::string>());
                    if (!color) throw std::runtime_error("text span has invalid highlight background color");
                    highlight.background = *color;
                }
                if (raw_highlight.contains("padding_em"))
                    highlight.padding_em = raw_highlight.at("padding_em").get<float>();
                if (raw_highlight.contains("radius_em"))
                    highlight.radius_em = raw_highlight.at("radius_em").get<float>();
                span.highlight = highlight;
            }
            if (raw_span.contains("semantic_id")) span.semantic_id = raw_span.at("semantic_id").get<std::string>();
            layer.text_spans.push_back(std::move(span));
        }
    }

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
        if (raw_shape.contains("asset")) {
            if (shape.type != ShapeContentTypePlan::Path || raw_shape.contains("path"))
                throw std::runtime_error("shape.asset is only valid for path shapes and cannot be combined with path commands");
            shape.svg_asset = raw_shape.at("asset").get<std::string>();
            if (detail::invalid_logical_path(*shape.svg_asset) ||
                std::filesystem::path(*shape.svg_asset).extension() != ".svg")
                throw std::runtime_error("shape.asset must be a relative logical .svg path");
        }
        if (raw_shape.contains("lottie")) {
            if (shape.type != ShapeContentTypePlan::Path || raw_shape.contains("path") ||
                raw_shape.contains("asset"))
                throw std::runtime_error("shape.lottie is only valid for path shapes and cannot be combined with path commands or SVG");
            shape.lottie_asset = raw_shape.at("lottie").get<std::string>();
            if (detail::invalid_logical_path(*shape.lottie_asset) ||
                std::filesystem::path(*shape.lottie_asset).extension() != ".json")
                throw std::runtime_error("shape.lottie must be a relative logical .json path");
        }

        if (raw_shape.contains("fill_rule")) {
            if (shape.type != ShapeContentTypePlan::Path)
                throw std::runtime_error("shape.fill_rule is only valid for path shapes");
            const auto fill_rule = raw_shape.at("fill_rule").get<std::string>();
            if (fill_rule == "even_odd") shape.fill_rule = ShapeFillRulePlan::EvenOdd;
            else if (fill_rule == "non_zero") shape.fill_rule = ShapeFillRulePlan::NonZero;
            else throw std::runtime_error("shape.fill_rule must be 'even_odd' or 'non_zero'");
        }
        if (layer.size_dimensions != 2 || !std::isfinite(layer.size[0]) ||
            !std::isfinite(layer.size[1]) || layer.size[0] <= 0.0f || layer.size[1] <= 0.0f)
            throw std::runtime_error("shape layers require positive two-dimensional size");
        shape.radius = raw_shape.value("radius", 0.0f);
        if (raw_shape.contains("corner_radius")) {
            if (shape.type != ShapeContentTypePlan::RoundedRect)
                throw std::runtime_error("shape.corner_radius is only valid for rounded_rect");
            if (raw_shape.contains("radius"))
                throw std::runtime_error("shape.radius and shape.corner_radius are mutually exclusive");
            const auto& corners = raw_shape.at("corner_radius");
            if (!corners.is_array() || corners.size() != 4)
                throw std::runtime_error("shape.corner_radius must contain [tl, tr, br, bl]");
            std::array<float, 4> decoded{};
            const float max_radius = std::min(layer.size[0], layer.size[1]) * 0.5f;
            for (std::size_t i = 0; i < decoded.size(); ++i) {
                decoded[i] = corners.at(i).get<float>();
                if (!std::isfinite(decoded[i]) || decoded[i] < 0.0f || decoded[i] > max_radius)
                    throw std::runtime_error("rounded_rect corner radius must be finite, non-negative, and not exceed half the smaller size dimension");
            }
            shape.corner_radius = decoded;
        }
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
             (shape.type == ShapeContentTypePlan::Path && !shape.svg_asset &&
              !shape.lottie_asset)) && shape.path.empty())
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
                } else if (kind == "morph") {
                    decoded_op.kind = PathOperatorKindPlan::Morph;
                    if (!p.contains("target"))
                        throw std::runtime_error("morph operator requires target commands");
                    const auto& raw_target = p.at("target");
                    if (!raw_target.is_array() || raw_target.empty() || raw_target.size() > 65536)
                        throw std::runtime_error("morph target must contain between 1 and 65536 commands");
                    for (const auto& raw_command : raw_target) {
                        ShapePathCommandPlan command;
                        const auto type = raw_command.at("type").get<std::string>();
                        if (type == "move_to") command.type = ShapePathCommandTypePlan::MoveTo;
                        else if (type == "line_to") command.type = ShapePathCommandTypePlan::LineTo;
                        else if (type == "quadratic_to") command.type = ShapePathCommandTypePlan::QuadraticTo;
                        else if (type == "cubic_to") command.type = ShapePathCommandTypePlan::CubicTo;
                        else if (type == "close") command.type = ShapePathCommandTypePlan::Close;
                        else throw std::runtime_error("unsupported morph path command: " + type);
                        const auto read_point = [](const nlohmann::json& value,
                                                   std::array<float, 2>& out) {
                            if (!value.is_array() || value.size() != 2)
                                throw std::runtime_error("morph path points must contain two values");
                            out = {value[0].get<float>(), value[1].get<float>()};
                        };
                        const bool has_point = raw_command.contains("point");
                        const bool has_c1 = raw_command.contains("control1");
                        const bool has_c2 = raw_command.contains("control2");
                        if (has_point) read_point(raw_command.at("point"), command.point);
                        if (has_c1) read_point(raw_command.at("control1"), command.control1);
                        if (has_c2) read_point(raw_command.at("control2"), command.control2);
                        if ((command.type == ShapePathCommandTypePlan::MoveTo ||
                             command.type == ShapePathCommandTypePlan::LineTo) &&
                            (!has_point || has_c1 || has_c2))
                            throw std::runtime_error("morph move_to/line_to require point only");
                        if (command.type == ShapePathCommandTypePlan::QuadraticTo &&
                            (!has_point || !has_c1 || has_c2))
                            throw std::runtime_error("morph quadratic_to requires point and control1");
                        if (command.type == ShapePathCommandTypePlan::CubicTo &&
                            (!has_point || !has_c1 || !has_c2))
                            throw std::runtime_error("morph cubic_to requires point and both controls");
                        if (command.type == ShapePathCommandTypePlan::Close &&
                            (has_point || has_c1 || has_c2))
                            throw std::runtime_error("morph close accepts no coordinates");
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
                    if (const auto topology_error = validate_morph_path_topology(
                            source_commands, target_commands))
                        throw std::runtime_error(*topology_error);
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
            if (stroke.contains("dash_array")) {
                const auto& dash = stroke.at("dash_array");
                if (!dash.is_array()) throw std::runtime_error("stroke.dash_array must be an array");
                for (const auto& entry : dash) {
                    const float value = entry.get<float>();
                    if (!std::isfinite(value) || value < 0.0f)
                        throw std::runtime_error("stroke.dash_array entries must be finite and >= 0");
                    decoded_stroke.dash_array.push_back(value);
                }
            }
            if (stroke.contains("dash_offset")) {
                decoded_stroke.dash_offset = stroke.at("dash_offset").get<float>();
                if (!std::isfinite(decoded_stroke.dash_offset))
                    throw std::runtime_error("stroke.dash_offset must be finite");
            }
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
    if (value.contains("fill_gradient_animation")) {
        if (layer.animation && std::any_of(layer.animation->tracks.begin(),
                layer.animation->tracks.end(), [](const AnimationTrackPlan& track) {
                    return track.property == "fill_color";
                }))
            throw std::runtime_error(
                "fill_color and fill_gradient_animation tracks are mutually exclusive");
        if (layer.type != LayerType::Shape || !layer.shape ||
            layer.shape->type == ShapeContentTypePlan::Line ||
            layer.shape->type == ShapeContentTypePlan::Path ||
            !layer.shape->gradient) {
            throw std::runtime_error(
                "fill_gradient_animation requires a filled rect, rounded_rect, or ellipse shape");
        }
        auto animation = decode_gradient_animation(value.at("fill_gradient_animation"));
        if (animation.keyframes.empty())
            throw std::runtime_error("fill_gradient_animation requires keyframes");
        const auto& base = *layer.shape->gradient;
        const auto compatible = [&](const graphics::GradientDefinition& candidate) {
            return candidate.type == base.type && candidate.spread == base.spread;
        };
        for (const auto& key : animation.keyframes) {
            if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                !compatible(key.value))
                throw std::runtime_error(
                    "fill gradient keyframes must use the base gradient type and spread");
        }
        std::stable_sort(animation.keyframes.begin(), animation.keyframes.end(),
            [](const auto& a, const auto& b) { return a.frame < b.frame; });
        for (std::size_t index = 1; index < animation.keyframes.size(); ++index)
            if (animation.keyframes[index - 1].frame == animation.keyframes[index].frame)
                throw std::runtime_error("fill gradient keyframe frames must be unique");
        layer.fill_gradient_animation = std::move(animation);
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
    if (value.contains("effects")) {
        const auto& effects = value.at("effects");
        if (!effects.is_array() || effects.size() > 64)
            throw std::runtime_error("layer.effects must be an array of at most 64 entries");
        for (const auto& raw_effect : effects) {
            LayerPlan::EffectPlan effect;
            const auto kind = raw_effect.value("type", std::string{});
            if (raw_effect.contains("time")) {
                const auto& time=raw_effect.at("time");
                if(!time.is_object()) throw std::runtime_error("effect time must be an object");
                if(time.contains("posterize_fps")) {
                    effect.posterize_fps=time.at("posterize_fps").get<float>();
                    if(!std::isfinite(*effect.posterize_fps)||*effect.posterize_fps<=0.0f||*effect.posterize_fps>1000.0f)
                        throw std::runtime_error("effect time posterize_fps must be finite and in (0, 1000]");
                }
                if(time.contains("curve")) {
                    const auto& raw_curve=time.at("curve");
                    if(!raw_curve.is_array()||raw_curve.size()<2) throw std::runtime_error("effect time curve must contain at least two keyframes");
                    AnimationTrackPlan curve; curve.property="effect_time";
                    curve.easing=time.value("easing",std::string{"linear"});
                    curve.bezier_curve=decode_bezier_curve(time,curve.easing,"effect time");
                    for(const auto& raw_key:raw_curve) {
                        AnimationKeyframePlan key;
                        key.frame=Frame{raw_key.at("frame").get<std::int64_t>()};
                        const float v=raw_key.at("value").get<float>();
                        if(key.frame.value<0||key.frame.value>1000000||!std::isfinite(v)||v<0.0f||v>1.0f)
                            throw std::runtime_error("effect time curve keyframes require bounded frame values and values in [0, 1]");
                        key.value={v}; curve.keyframes.push_back(std::move(key));
                    }
                    std::sort(curve.keyframes.begin(),curve.keyframes.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
                    for(std::size_t i=1;i<curve.keyframes.size();++i)
                        if(curve.keyframes[i-1].frame==curve.keyframes[i].frame) throw std::runtime_error("effect time curve frames must be unique");
                    effect.time_curve=std::move(curve);
                } else if(time.contains("easing")||time.contains("bezier_curve"))
                    throw std::runtime_error("effect time easing requires a curve");
                for(auto it=time.begin();it!=time.end();++it)
                    if(it.key()!="posterize_fps"&&it.key()!="curve"&&it.key()!="easing"&&it.key()!="bezier_curve")
                        throw std::runtime_error("unsupported effect time option: "+it.key());
            }
            if (kind == "lut_3d") {
                effect.kind = LayerPlan::EffectKindPlan::Lut3D;
                effect.asset = raw_effect.at("asset").get<std::string>();
                effect.amount = raw_effect.value("amount", 1.0f);
                if (effect.asset.empty() || effect.asset.size() > 4096 ||
                    !std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 1.0f)
                    throw std::runtime_error("3D LUT effect requires an asset and amount in [0, 1]");
            } else if (kind == "echo") {
                effect.kind = LayerPlan::EffectKindPlan::Echo;
                effect.echoes = raw_effect.value("echoes", 3u);
                effect.decay = raw_effect.value("decay", 0.5f);
                effect.time_offset_frames = raw_effect.value("time_offset_frames", 1u);
                if (effect.echoes == 0 || effect.echoes > 8 || !std::isfinite(effect.decay) ||
                    effect.decay < 0.0f || effect.decay > 1.0f ||
                    effect.time_offset_frames == 0 || effect.time_offset_frames > 120)
                    throw std::runtime_error("Echo requires echoes in [1, 8], decay in [0, 1], and time_offset_frames in [1, 120]");
            } else if (kind == "keying") {
                effect.kind = LayerPlan::EffectKindPlan::Keying;
                const auto mode = raw_effect.value("mode", std::string("chroma"));
                if (mode == "luma") effect.keying_mode = LayerPlan::KeyingModePlan::Luma;
                else if (mode == "chroma") effect.keying_mode = LayerPlan::KeyingModePlan::Chroma;
                else throw std::runtime_error("Keying mode must be 'luma' or 'chroma'");
                if (raw_effect.contains("key_color")) {
                    const auto& color = raw_effect.at("key_color");
                    if (!color.is_array() || (color.size() != 3 && color.size() != 4))
                        throw std::runtime_error("Keying key_color must contain RGB or RGBA");
                    for (std::size_t i = 0; i < 3; ++i) {
                        effect.key_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.key_color[i]) || effect.key_color[i] < 0.0f || effect.key_color[i] > 1.0f)
                            throw std::runtime_error("Keying key_color components must be in [0, 1]");
                    }
                }
                effect.threshold = raw_effect.value("threshold", 0.20f);
                effect.softness = raw_effect.value("softness", 0.08f);
                effect.spill = raw_effect.value("spill", 0.75f);
                effect.clip_black = raw_effect.value("clip_black", 0.0f);
                effect.clip_white = raw_effect.value("clip_white", 1.0f);
                effect.choke = raw_effect.value("choke", 0.0f);
                if (raw_effect.contains("garbage_rect")) {
                    const auto& rect = raw_effect.at("garbage_rect");
                    if (!rect.is_array() || rect.size() != 4)
                        throw std::runtime_error("Keying garbage_rect must contain [x, y, w, h]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.garbage_rect[i] = rect.at(i).get<float>();
                        if (!std::isfinite(effect.garbage_rect[i]) ||
                            effect.garbage_rect[i] < 0.0f || effect.garbage_rect[i] > 1.0f)
                            throw std::runtime_error("Keying garbage_rect components must be in [0, 1]");
                    }
                }
                effect.garbage_feather = raw_effect.value("garbage_feather", 0.0f);
                if (!std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f ||
                    !std::isfinite(effect.softness) || effect.softness < 0.0f || effect.softness > 1.0f ||
                    !std::isfinite(effect.spill) || effect.spill < 0.0f || effect.spill > 1.0f)
                    throw std::runtime_error("Keying threshold, softness, and spill must be in [0, 1]");
                if (!std::isfinite(effect.clip_black) || !std::isfinite(effect.clip_white) ||
                    effect.clip_black < 0.0f || effect.clip_white > 1.0f ||
                    effect.clip_white <= effect.clip_black)
                    throw std::runtime_error("Keying clip_black/clip_white must satisfy 0 <= black < white <= 1");
                if (!std::isfinite(effect.choke) || effect.choke < 0.0f || effect.choke >= 1.0f)
                    throw std::runtime_error("Keying choke must be in [0, 1)");
                if (!std::isfinite(effect.garbage_feather) || effect.garbage_feather < 0.0f ||
                    effect.garbage_feather > 0.5f)
                    throw std::runtime_error("Keying garbage_feather must be in [0, 0.5]");
            } else if (kind == "threshold") {
                effect.kind = LayerPlan::EffectKindPlan::Threshold;
                effect.threshold = raw_effect.value("threshold", 0.5f);
                effect.threshold_softness = raw_effect.value("softness", 0.0f);
                if (!std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f ||
                    !std::isfinite(effect.threshold_softness) || effect.threshold_softness < 0.0f || effect.threshold_softness > 1.0f)
                    throw std::runtime_error("Threshold and softness must be in [0, 1]");
            } else if (kind == "mosaic") {
                effect.kind = LayerPlan::EffectKindPlan::Mosaic;
                effect.block_size = raw_effect.value("block_size", 8u);
                if (effect.block_size == 0 || effect.block_size > 512)
                    throw std::runtime_error("Mosaic block_size must be in [1, 512]");
            } else if (kind == "gaussian_blur") {
                effect.kind = LayerPlan::EffectKindPlan::GaussianBlur;
                effect.radius = raw_effect.value("radius", 8.0f);
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f)
                    throw std::runtime_error("Gaussian blur radius must be in [0, 256]");
            } else if (kind == "drop_shadow") {
                effect.kind = LayerPlan::EffectKindPlan::DropShadow;
                effect.radius = raw_effect.value("radius", 12.0f);
                if (raw_effect.contains("offset")) {
                    const auto& offset = raw_effect.at("offset");
                    if (!offset.is_array() || offset.size() != 2)
                        throw std::runtime_error("Drop shadow offset must contain [x, y]");
                    for (std::size_t i = 0; i < 2; ++i) {
                        effect.offset[i] = offset.at(i).get<float>();
                        if (!std::isfinite(effect.offset[i]) || std::abs(effect.offset[i]) > 10000.0f)
                            throw std::runtime_error("Drop shadow offset values must be finite and within [-10000, 10000]");
                    }
                }
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Drop shadow color must contain [r, g, b, a]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.effect_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.effect_color[i]) || effect.effect_color[i] < 0.0f || effect.effect_color[i] > 1.0f)
                            throw std::runtime_error("Drop shadow color components must be in [0, 1]");
                    }
                }
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f)
                    throw std::runtime_error("Drop shadow radius must be in [0, 256]");
            } else if (kind == "bloom") {
                effect.kind = LayerPlan::EffectKindPlan::Bloom;
                effect.bloom_threshold = raw_effect.value("threshold", 0.80f);
                effect.radius = raw_effect.value("radius", 24.0f);
                effect.bloom_intensity = raw_effect.value("intensity", 0.60f);
                if (!std::isfinite(effect.bloom_threshold) || effect.bloom_threshold < 0.0f || effect.bloom_threshold > 1.0f ||
                    !std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f ||
                    !std::isfinite(effect.bloom_intensity) || effect.bloom_intensity < 0.0f || effect.bloom_intensity > 4.0f)
                    throw std::runtime_error("Bloom requires threshold in [0, 1], radius in [0, 256], and intensity in [0, 4]");
            } else if (kind == "glow") {
                effect.kind = LayerPlan::EffectKindPlan::Glow;
                effect.radius = raw_effect.value("radius", 24.0f);
                effect.glow_intensity = raw_effect.value("intensity", 0.25f);
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Glow color must contain [r, g, b, a]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.effect_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.effect_color[i]) || effect.effect_color[i] < 0.0f || effect.effect_color[i] > 1.0f)
                            throw std::runtime_error("Glow color components must be in [0, 1]");
                    }
                }
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f ||
                    !std::isfinite(effect.glow_intensity) || effect.glow_intensity < 0.0f || effect.glow_intensity > 4.0f)
                    throw std::runtime_error("Glow requires radius in [0, 256] and intensity in [0, 4]");
            } else if (kind == "light_rays") {
                effect.kind = LayerPlan::EffectKindPlan::LightRays;
                if (raw_effect.contains("origin")) {
                    const auto& origin = raw_effect.at("origin");
                    if (!origin.is_array() || origin.size() != 2)
                        throw std::runtime_error("Light rays origin must contain [x, y]");
                    effect.light_rays_origin = {origin[0].get<float>(), origin[1].get<float>()};
                }
                effect.light_rays_length = raw_effect.value("length", 320.0f);
                effect.light_rays_density = raw_effect.value("density", 0.75f);
                effect.light_rays_decay = raw_effect.value("decay", 0.92f);
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Light rays color must contain RGBA");
                    for (std::size_t i = 0; i < 4; ++i)
                        effect.light_rays_color[i] = color[i].get<float>();
                }
                if (!std::isfinite(effect.light_rays_origin[0]) ||
                    !std::isfinite(effect.light_rays_origin[1]) ||
                    !std::isfinite(effect.light_rays_length) || effect.light_rays_length < 0.0f ||
                    effect.light_rays_length > 100000.0f ||
                    effect.light_rays_origin[0] < 0.0f || effect.light_rays_origin[0] > 1.0f ||
                    effect.light_rays_origin[1] < 0.0f || effect.light_rays_origin[1] > 1.0f ||
                    !std::isfinite(effect.light_rays_density) || effect.light_rays_density < 0.0f ||
                    effect.light_rays_density > 8.0f ||
                    !std::isfinite(effect.light_rays_decay) || effect.light_rays_decay < 0.0f ||
                    effect.light_rays_decay > 1.0f ||
                    std::any_of(effect.light_rays_color.begin(), effect.light_rays_color.end(),
                        [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        }))
                    throw std::runtime_error("Light rays parameters are outside supported finite ranges");
            } else if (kind == "noise") {
                effect.kind = LayerPlan::EffectKindPlan::Noise;
                effect.amount = raw_effect.value("amount", 0.08f);
                effect.seed = raw_effect.value("seed", std::uint32_t{0});
                effect.animated = raw_effect.value("animated", false);
                effect.noise_size = raw_effect.value("size", 1.0f);
                effect.grain_shadow_response = raw_effect.value("shadow_response", 1.0f);
                effect.grain_midtone_response = raw_effect.value("midtone_response", 1.0f);
                effect.grain_highlight_response = raw_effect.value("highlight_response", 1.0f);
                const auto mode = raw_effect.value("color_mode", std::string{"monochrome"});
                if (mode != "monochrome" && mode != "rgb")
                    throw std::runtime_error("Noise color_mode must be monochrome or rgb");
                effect.rgb_noise = mode == "rgb";
                if (!std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 1.0f ||
                    !std::isfinite(effect.noise_size) || effect.noise_size <= 0.0f || effect.noise_size > 256.0f ||
                    !std::isfinite(effect.grain_shadow_response) || effect.grain_shadow_response < 0.0f || effect.grain_shadow_response > 4.0f ||
                    !std::isfinite(effect.grain_midtone_response) || effect.grain_midtone_response < 0.0f || effect.grain_midtone_response > 4.0f ||
                    !std::isfinite(effect.grain_highlight_response) || effect.grain_highlight_response < 0.0f || effect.grain_highlight_response > 4.0f)
                    throw std::runtime_error("Noise amount, size, or tonal response is outside its supported range");
            } else if (kind == "fractal_noise") {
                effect.kind = LayerPlan::EffectKindPlan::FractalNoise;
                effect.seed = raw_effect.value("seed", std::uint32_t{0});
                effect.octaves = raw_effect.value("octaves", std::uint32_t{4});
                effect.frequency = raw_effect.value("frequency", 0.015f);
                effect.amplitude = raw_effect.value("amplitude", 1.0f);
                effect.evolution = raw_effect.value("evolution", 0.0f);
                effect.contrast = raw_effect.value("contrast", 1.0f);
                effect.brightness = raw_effect.value("brightness", 0.0f);
                const auto mode = raw_effect.value("color_mode", std::string{"monochrome"});
                if (mode != "monochrome" && mode != "rgb")
                    throw std::runtime_error("Fractal Noise color_mode must be monochrome or rgb");
                effect.rgb_noise = mode == "rgb";
                if (effect.octaves == 0 || effect.octaves > 16 ||
                    !std::isfinite(effect.frequency) || effect.frequency <= 0.0f || effect.frequency > 10.0f ||
                    !std::isfinite(effect.amplitude) || effect.amplitude < 0.0f || effect.amplitude > 4.0f ||
                    !std::isfinite(effect.evolution) || std::abs(effect.evolution) > 10000.0f ||
                    !std::isfinite(effect.contrast) || effect.contrast < 0.0f || effect.contrast > 16.0f ||
                    !std::isfinite(effect.brightness) || std::abs(effect.brightness) > 16.0f)
                    throw std::runtime_error("Fractal Noise parameters are outside supported finite ranges");
            } else if (kind == "sharpen") {
                effect.kind = LayerPlan::EffectKindPlan::Sharpen;
                effect.amount = raw_effect.value("amount", 0.0f);
                effect.radius = raw_effect.value("radius", 1.0f);
                effect.threshold = raw_effect.value("threshold", 0.0f);
                if (!std::isfinite(effect.amount) || effect.amount < -4.0f || effect.amount > 4.0f ||
                    !std::isfinite(effect.radius) || effect.radius <= 0.0f || effect.radius > 64.0f ||
                    !std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f)
                    throw std::runtime_error("Invalid sharpen parameters");
            } else if (kind == "turbulent_displace") {
                effect.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
                effect.amount=raw_effect.value("amount",0.1f); effect.warp_size=raw_effect.value("size",0.1f);
                effect.warp_evolution=raw_effect.value("evolution",0.0f); effect.warp_speed=raw_effect.value("evolution_speed",0.0f);
                effect.warp_complexity=raw_effect.value("complexity",2U);
                if(!std::isfinite(effect.amount)||effect.amount<0||effect.amount>1||!std::isfinite(effect.warp_size)||effect.warp_size<=0||
                   !std::isfinite(effect.warp_evolution)||!std::isfinite(effect.warp_speed)||effect.warp_complexity<1||effect.warp_complexity>16)
                    throw std::runtime_error("Invalid turbulent displace parameters");
            } else if (kind == "wave_warp") {
                effect.kind=LayerPlan::EffectKindPlan::WaveWarp; effect.amount=raw_effect.value("amplitude",0.05f);
                effect.warp_wavelength=raw_effect.value("wavelength",0.2f); effect.warp_speed=raw_effect.value("speed",1.0f); effect.warp_phase=raw_effect.value("phase",0.0f);
                if(raw_effect.contains("direction")) { const auto& v=raw_effect.at("direction"); if(!v.is_array()||v.size()!=2) throw std::runtime_error("wave direction requires two values"); effect.warp_direction={v[0].get<float>(),v[1].get<float>()}; }
                const auto type=raw_effect.value("wave_type",std::string{"sine"});
                if(type=="triangle") effect.wave_type=1; else if(type=="square") effect.wave_type=2; else if(type=="saw") effect.wave_type=3; else if(type!="sine") throw std::runtime_error("Invalid wave type");
                if(!std::isfinite(effect.amount)||!std::isfinite(effect.warp_wavelength)||effect.warp_wavelength<=0||!std::isfinite(effect.warp_speed)||!std::isfinite(effect.warp_phase)||!std::isfinite(effect.warp_direction[0])||!std::isfinite(effect.warp_direction[1])) throw std::runtime_error("Invalid wave warp parameters");
            } else if (kind == "ripple") {
                effect.kind=LayerPlan::EffectKindPlan::Ripple; effect.warp_radius=raw_effect.value("radius",0.25f);
                effect.warp_speed=raw_effect.value("wave_speed",1.5f); effect.warp_width=raw_effect.value("width",0.1f);
                effect.warp_height=raw_effect.value("wave_height",0.05f); effect.warp_damping=raw_effect.value("damping",0.5f);
                if(raw_effect.contains("center")) { const auto& v=raw_effect.at("center"); if(!v.is_array()||v.size()!=2) throw std::runtime_error("ripple center requires two values"); effect.warp_center={v[0].get<float>(),v[1].get<float>()}; }
                if(!std::isfinite(effect.warp_radius)||effect.warp_radius<=0||!std::isfinite(effect.warp_speed)||!std::isfinite(effect.warp_width)||effect.warp_width<=0||!std::isfinite(effect.warp_height)||!std::isfinite(effect.warp_damping)||effect.warp_damping<0||!std::isfinite(effect.warp_center[0])||!std::isfinite(effect.warp_center[1])) throw std::runtime_error("Invalid ripple parameters");
            } else if(kind=="displacement_map") {
                effect.kind=LayerPlan::EffectKindPlan::DisplacementMap;
                effect.map_layer=raw_effect.at("map_layer").get<std::string>();
                effect.map_intensity_x=raw_effect.value("intensity_x",0.05f);
                effect.map_intensity_y=raw_effect.value("intensity_y",0.05f);
                const auto channel=raw_effect.value("map_channel",std::string{"luma"});
                if(channel=="red") effect.map_channel=1; else if(channel=="green") effect.map_channel=2;
                else if(channel=="blue") effect.map_channel=3; else if(channel=="alpha") effect.map_channel=4;
                else if(channel!="luma") throw std::runtime_error("Unsupported displacement map channel");
                if(effect.map_layer.empty()||!std::isfinite(effect.map_intensity_x)||!std::isfinite(effect.map_intensity_y)||
                   std::abs(effect.map_intensity_x)>4.0f||std::abs(effect.map_intensity_y)>4.0f)
                    throw std::runtime_error("Invalid displacement map parameters");
            } else if (kind == "corner_pin") {
                effect.kind = LayerPlan::EffectKindPlan::CornerPin;
                const auto read_quad = [&](const char* key, std::size_t index) {
                    if (!raw_effect.contains(key)) return;
                    const auto& point = raw_effect.at(key);
                    if (!point.is_array() || point.size() != 2)
                        throw std::runtime_error(std::string("Corner pin ") + key + " requires two values");
                    effect.quad[index] = point[0].get<float>();
                    effect.quad[index + 1] = point[1].get<float>();
                };
                read_quad("top_left", 0);
                read_quad("top_right", 2);
                read_quad("bottom_right", 4);
                read_quad("bottom_left", 6);
                for (const float value : effect.quad)
                    if (!std::isfinite(value) || std::abs(value) > 4.0f)
                        throw std::runtime_error("Corner pin coordinates must be finite and within [-4, 4]");
            } else if (kind == "twirl") {
                effect.kind = LayerPlan::EffectKindPlan::Twirl;
                if (raw_effect.contains("center")) {
                    const auto& center = raw_effect.at("center");
                    if (!center.is_array() || center.size() != 2)
                        throw std::runtime_error("Twirl center requires two values");
                    effect.warp_center = {center[0].get<float>(), center[1].get<float>()};
                }
                effect.warp_angle = raw_effect.value("angle_degrees", 90.0f);
                effect.warp_radius = raw_effect.value("radius", 0.5f);
                effect.warp_falloff = raw_effect.value("falloff", 1.0f);
                if (!std::isfinite(effect.warp_center[0]) || !std::isfinite(effect.warp_center[1]) ||
                    !std::isfinite(effect.warp_angle) || std::abs(effect.warp_angle) > 3600.0f ||
                    !std::isfinite(effect.warp_radius) || effect.warp_radius <= 0.0f ||
                    effect.warp_radius > 2.0f ||
                    !std::isfinite(effect.warp_falloff) || effect.warp_falloff <= 0.0f ||
                    effect.warp_falloff > 4.0f)
                    throw std::runtime_error("Invalid twirl parameters");
            } else if (kind == "bulge") {
                effect.kind = LayerPlan::EffectKindPlan::Bulge;
                if (raw_effect.contains("center")) {
                    const auto& center = raw_effect.at("center");
                    if (!center.is_array() || center.size() != 2)
                        throw std::runtime_error("Bulge center requires two values");
                    effect.warp_center = {center[0].get<float>(), center[1].get<float>()};
                }
                effect.warp_amount = raw_effect.value("amount", 0.5f);
                effect.warp_radius = raw_effect.value("radius", 0.5f);
                if (!std::isfinite(effect.warp_center[0]) || !std::isfinite(effect.warp_center[1]) ||
                    !std::isfinite(effect.warp_amount) || effect.warp_amount < -1.0f ||
                    effect.warp_amount > 1.0f ||
                    !std::isfinite(effect.warp_radius) || effect.warp_radius <= 0.0f ||
                    effect.warp_radius > 2.0f)
                    throw std::runtime_error("Invalid bulge parameters");
            } else if (kind == "mesh_warp") {
                effect.kind = LayerPlan::EffectKindPlan::MeshWarp;
                effect.mesh_cols = raw_effect.value("cols", 2u);
                effect.mesh_rows = raw_effect.value("rows", 2u);
                effect.mesh_smoothness = raw_effect.value("smoothness", 0.0f);
                if (effect.mesh_cols < 2 || effect.mesh_cols > 32 ||
                    effect.mesh_rows < 2 || effect.mesh_rows > 32 ||
                    !std::isfinite(effect.mesh_smoothness) || effect.mesh_smoothness < 0.0f ||
                    effect.mesh_smoothness > 1.0f)
                    throw std::runtime_error("Invalid mesh-warp grid dimensions");
                const auto& offsets = raw_effect.at("offsets");
                const std::size_t expected =
                    static_cast<std::size_t>(effect.mesh_cols) * effect.mesh_rows * 2;
                if (!offsets.is_array() || offsets.size() != expected || expected > 2048)
                    throw std::runtime_error("Mesh warp offsets must contain cols*rows x,y pairs");
                effect.mesh_offsets.reserve(expected);
                for (const auto& value : offsets) {
                    const float component = value.get<float>();
                    if (!std::isfinite(component) || std::abs(component) > 4.0f)
                        throw std::runtime_error("Mesh warp offsets must be finite and within [-4, 4]");
                    effect.mesh_offsets.push_back(component);
                }

            } else throw std::runtime_error("unsupported layer effect type");
            layer.effects.push_back(std::move(effect));
        }
    }
    if (layer.type == LayerType::Adjustment) {
        if (layer.effects.empty())
            throw std::runtime_error("adjustment layers require at least one effect");
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() != "id" && it.key() != "type" && it.key() != "effects")
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
    layer.enable_3d = value.value("enable_3d", false);
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
            if (!std::isfinite(backdrop.blur) || backdrop.blur < 0.0f || backdrop.blur > 64.0f ||
                !std::isfinite(backdrop.brightness) || backdrop.brightness < 0.0f || backdrop.brightness > 4.0f ||
                !std::isfinite(backdrop.saturation) || backdrop.saturation < 0.0f || backdrop.saturation > 4.0f)
                throw std::runtime_error("style.backdrop blur, brightness, and saturation must be finite and within supported bounds");
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
        for (const auto& track_value : value.at("animation").at("tracks"))
            animation.tracks.push_back(decode_animation_track(track_value));
        layer.animation = std::move(animation);
    }
    if (layer.animation) {
        for (auto& track : layer.animation->tracks) {
            if (track.property != "fill_color") continue;
            std::stable_sort(track.keyframes.begin(), track.keyframes.end(),
                [](const auto& left, const auto& right) {
                    return left.frame < right.frame;
                });
        }
        const AnimationTrackPlan* fill_color_track = nullptr;
        for (const auto& track : layer.animation->tracks) {
            if (track.property == "stroke_dash_offset") {
                if (!layer.shape || !layer.shape->stroke)
                    throw std::runtime_error("stroke_dash_offset animation requires a shape with a stroke");
                for (const auto& key : track.keyframes)
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()))
                        throw std::runtime_error("stroke_dash_offset keyframes must be finite scalar values");
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
                            layer.shape->type == ShapeContentTypePlan::Ellipse) &&
                           layer.shape->fill_color && !layer.shape->gradient) {
                    // Only explicit solid fills are animatable; gradients use
                    // the typed fill_gradient_animation track instead.
                } else {
                    throw std::runtime_error(
                        "fill_color animation requires a color layer or a solid filled rect, rounded_rect, or ellipse");
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
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>()))
            layer.blend_mode = mode;
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    return layer;
}

}  // namespace chronon3d::render_plan::detail
