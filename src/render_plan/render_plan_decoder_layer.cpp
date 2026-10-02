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

std::optional<Color> decode_text_span_color(const std::string& encoded) {
    if (encoded.size() != 7 && encoded.size() != 9) return std::nullopt;
    const auto rgb = parse_hex_color(std::string_view(encoded).substr(0, 7));
    if (!rgb) return std::nullopt;
    if (encoded.size() == 7) return rgb;
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    const int high = nibble(encoded[7]);
    const int low = nibble(encoded[8]);
    if (high < 0 || low < 0) return std::nullopt;
    return Color{rgb->r, rgb->g, rgb->b,
        static_cast<float>(high * 16 + low) / 255.0f};
}

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

LayerPlan::StaggerPlan decode_stagger(const nlohmann::json& raw) {
    LayerPlan::StaggerPlan plan;
    plan.per_item_frames = raw.value("per_item_frames", plan.per_item_frames);
    plan.order = raw.value("order", plan.order);
    plan.seed = raw.value("seed", plan.seed);
    plan.track_property = raw.value("track_property", plan.track_property);
    plan.easing = raw.value("easing", plan.easing);
    if (plan.per_item_frames > 600 ||
        (plan.order != "forward" && plan.order != "reverse" &&
         plan.order != "center_out" && plan.order != "random" &&
         plan.order != "from_edges" && plan.order != "spatial_x" &&
         plan.order != "spatial_y" && plan.order != "radial") ||
        plan.track_property.empty() || plan.track_property.size() > 128)
        throw std::runtime_error("layout stagger has invalid delay, order, or track_property");
    if (!plan.easing.empty()) {
        static constexpr std::array<std::string_view, 22> easings{
            "linear", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic",
            "in_out_cubic", "in_expo", "out_expo", "in_out_expo", "in_sine", "out_sine",
            "in_out_sine", "in_back", "out_back", "in_out_back", "in_elastic", "out_elastic",
            "in_out_elastic", "in_bounce", "out_bounce", "in_out_bounce"};
        if (std::find(easings.begin(), easings.end(), plan.easing) == easings.end() &&
            plan.easing != "smoothstep" && plan.easing != "hold" && plan.easing != "bezier")
            throw std::runtime_error("layout stagger has an unsupported easing");
    }
    return plan;
}

std::vector<ShapePathCommandPlan> decode_follow_path_commands(const nlohmann::json& raw) {
    if (!raw.is_array() || raw.size() < 2 || raw.size() > 65536)
        throw std::runtime_error("follow_path path_commands must contain 2..65536 commands");
    std::vector<ShapePathCommandPlan> commands;
    commands.reserve(raw.size());
    bool has_move = false;
    bool closed = false;
    for (const auto& item : raw) {
        auto command = decode_shape_path_command(item, "follow_path command");
        if (closed) throw std::runtime_error("follow_path commands cannot continue after close");
        if (command.type == ShapePathCommandTypePlan::MoveTo) {
            if (has_move || !commands.empty())
                throw std::runtime_error("follow_path accepts one continuous subpath");
            has_move = true;
        } else if (!has_move) {
            throw std::runtime_error("follow_path must start with move_to");
        }
        for (const auto* point : {&command.point, &command.control1, &command.control2})
            if (std::abs((*point)[0]) > 1000000.0f || std::abs((*point)[1]) > 1000000.0f)
                throw std::runtime_error("follow_path coordinates must be within [-1000000,1000000]");
        closed = command.type == ShapePathCommandTypePlan::Close;
        commands.push_back(command);
    }
    if (!has_move || commands.size() < 2)
        throw std::runtime_error("follow_path requires move_to and at least one segment");
    return commands;
}


void decode_background_look(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("background_look")) {
        const auto& raw_look = value.at("background_look");
        if (!raw_look.is_object())
            throw std::runtime_error("background_look must be an object");
        const auto look_type = raw_look.value("type", std::string{});
        const auto add_effect = [&](LayerPlan::EffectPlan effect) {
            if (layer.effects.size() >= 64)
                throw std::runtime_error("background_look expansion exceeds the 64-effect layer limit");
            layer.effects.push_back(std::move(effect));
        };
        if (look_type == "film_grade") {
            LayerPlan::EffectPlan grain;
            grain.kind = LayerPlan::EffectKindPlan::Noise;
            grain.amount = raw_look.value("grain", 0.06f);
            grain.seed = raw_look.value("seed", std::uint32_t{0});
            grain.noise_size = raw_look.value("grain_size", 1.0f);
            grain.grain_shadow_response = raw_look.value("shadow_response", 0.7f);
            grain.grain_midtone_response = raw_look.value("midtone_response", 1.0f);
            grain.grain_highlight_response = raw_look.value("highlight_response", 0.65f);
            if (!std::isfinite(grain.amount) || grain.amount < 0.0f || grain.amount > 1.0f ||
                !std::isfinite(grain.noise_size) || grain.noise_size <= 0.0f || grain.noise_size > 128.0f ||
                !std::isfinite(grain.grain_shadow_response) || grain.grain_shadow_response < 0.0f || grain.grain_shadow_response > 4.0f ||
                !std::isfinite(grain.grain_midtone_response) || grain.grain_midtone_response < 0.0f || grain.grain_midtone_response > 4.0f ||
                !std::isfinite(grain.grain_highlight_response) || grain.grain_highlight_response < 0.0f || grain.grain_highlight_response > 4.0f)
                throw std::runtime_error("film_grade grain settings are outside supported ranges");
            add_effect(std::move(grain));
            LayerPlan::EffectPlan vignette;
            vignette.kind = LayerPlan::EffectKindPlan::Vignette;
            vignette.effect_color = {0.0f, 0.0f, 0.0f, 1.0f};
            vignette.amount = raw_look.value("vignette", 0.22f);
            vignette.radius = raw_look.value("vignette_radius", 0.72f);
            vignette.softness = raw_look.value("vignette_softness", 0.55f);
            if (!std::isfinite(vignette.amount) || vignette.amount < 0.0f || vignette.amount > 1.0f ||
                !std::isfinite(vignette.radius) || vignette.radius < 0.0f || vignette.radius > 1.0f ||
                !std::isfinite(vignette.softness) || vignette.softness < 0.0f || vignette.softness > 1.0f)
                throw std::runtime_error("film_grade vignette settings are outside [0, 1]");
            add_effect(std::move(vignette));
            if (raw_look.contains("lut")) {
                LayerPlan::EffectPlan lut;
                lut.kind = LayerPlan::EffectKindPlan::Lut3D;
                lut.asset = raw_look.at("lut").get<std::string>();
                lut.amount = raw_look.value("lut_amount", 1.0f);
                if (lut.asset.empty() || !std::isfinite(lut.amount) || lut.amount < 0.0f || lut.amount > 1.0f)
                    throw std::runtime_error("film_grade LUT requires a non-empty asset and amount in [0, 1]");
                if (layer.effects.size() >= 64)
                    throw std::runtime_error("background_look expansion exceeds the 64-effect layer limit");
                layer.effects.insert(layer.effects.end() - 2, std::move(lut));
            }
        } else if (look_type == "aurora") {
            LayerPlan::EffectPlan noise;
            noise.kind = LayerPlan::EffectKindPlan::FractalNoise;
            noise.seed = raw_look.value("seed", std::uint32_t{0});
            noise.octaves = raw_look.value("octaves", 5U);
            noise.frequency = raw_look.value("frequency", 0.009f);
            noise.amplitude = raw_look.value("amplitude", 0.55f);
            noise.evolution = raw_look.value("evolution", 0.12f);
            noise.contrast = raw_look.value("contrast", 1.4f);
            noise.rgb_noise = true;
            if (noise.octaves == 0 || noise.octaves > 16 || !std::isfinite(noise.frequency) || noise.frequency <= 0.0f || noise.frequency > 10.0f ||
                !std::isfinite(noise.amplitude) || noise.amplitude < 0.0f || noise.amplitude > 4.0f || !std::isfinite(noise.evolution) || std::abs(noise.evolution) > 10000.0f ||
                !std::isfinite(noise.contrast) || noise.contrast < 0.0f || noise.contrast > 16.0f)
                throw std::runtime_error("aurora settings are outside supported FractalNoise ranges");
            add_effect(std::move(noise));
            LayerPlan::EffectPlan warp;
            warp.kind = LayerPlan::EffectKindPlan::WaveWarp;
            warp.amount = raw_look.value("warp", 0.025f);
            warp.warp_wavelength = raw_look.value("wavelength", 0.35f);
            warp.warp_speed = raw_look.value("speed", 0.3f);
            warp.wave_type = 0;
            if (!std::isfinite(warp.amount) || std::abs(warp.amount) > 1.0f || !std::isfinite(warp.warp_wavelength) || warp.warp_wavelength <= 0.0f ||
                !std::isfinite(warp.warp_speed) || std::abs(warp.warp_speed) > 1000.0f)
                throw std::runtime_error("aurora warp settings are outside supported WaveWarp ranges");
            add_effect(std::move(warp));
        } else if (look_type == "liquid") {
            LayerPlan::EffectPlan wave;
            wave.kind = LayerPlan::EffectKindPlan::WaveWarp;
            wave.amount = raw_look.value("amplitude", 0.025f);
            wave.warp_wavelength = raw_look.value("wavelength", 0.2f);
            wave.warp_speed = raw_look.value("speed", 0.8f);
            if (!std::isfinite(wave.amount) || std::abs(wave.amount) > 1.0f || !std::isfinite(wave.warp_wavelength) || wave.warp_wavelength <= 0.0f ||
                !std::isfinite(wave.warp_speed) || std::abs(wave.warp_speed) > 1000.0f)
                throw std::runtime_error("liquid settings are outside supported WaveWarp ranges");
            add_effect(std::move(wave));
            LayerPlan::EffectPlan ripple;
            ripple.kind = LayerPlan::EffectKindPlan::Ripple;
            ripple.warp_center = {0.5f, 0.5f};
            ripple.warp_radius = raw_look.value("radius", 0.55f);
            ripple.warp_speed = raw_look.value("ripple_speed", 0.35f);
            ripple.warp_width = raw_look.value("ripple_width", 0.08f);
            ripple.warp_height = raw_look.value("ripple_height", 0.015f);
            ripple.warp_damping = raw_look.value("damping", 0.6f);
            if (!std::isfinite(ripple.warp_radius) || ripple.warp_radius <= 0.0f || ripple.warp_radius > 2.0f ||
                !std::isfinite(ripple.warp_speed) || std::abs(ripple.warp_speed) > 1000.0f || !std::isfinite(ripple.warp_width) || ripple.warp_width <= 0.0f ||
                !std::isfinite(ripple.warp_height) || std::abs(ripple.warp_height) > 1.0f || !std::isfinite(ripple.warp_damping) || ripple.warp_damping < 0.0f || ripple.warp_damping > 1.0f)
                throw std::runtime_error("liquid ripple settings are outside supported ranges");
            add_effect(std::move(ripple));
        } else {
            throw std::runtime_error("background_look.type must be film_grade, aurora, or liquid");
        }
    }
}

void decode_effect_parameter_tracks(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("effect_param_tracks")) {
        if (!value.at("effect_param_tracks").is_array() || value.at("effect_param_tracks").size() > 256)
            throw std::runtime_error("effect_param_tracks must be an array of at most 256 entries");
        for (const auto& raw_track : value.at("effect_param_tracks")) {
            const auto effect_id = raw_track.at("effect_id").get<std::string>();
            const auto param = raw_track.at("param").get<std::string>();
            if (!chronon3d::effects::is_canonical_effect_id(effect_id))
                throw std::runtime_error("effect parameter track uses an unknown canonical effect_id: " + effect_id);
            const auto* definition = chronon3d::effects::find_animatable_effect_param(effect_id, param);
            if (!definition)
                throw std::runtime_error("effect parameter is not declared animatable: " + effect_id + "." + param);
            const bool target_exists = std::any_of(layer.effects.begin(), layer.effects.end(),
                [effect_id](const LayerPlan::EffectPlan& effect) {
                    return effect.kind == LayerPlan::EffectKindPlan::Catalog
                        ? effect.canonical_effect_id == effect_id
                        : effect_id_for_plan_kind(effect.kind) == effect_id;
                });
            if (!target_exists)
                throw std::runtime_error("effect parameter track does not target an effect in this layer: " + effect_id);

            auto raw_animation = raw_track;
            raw_animation.erase("effect_id");
            raw_animation.erase("param");
            raw_animation["property"] = "opacity";
            auto track = decode_animation_track(raw_animation);
            if (track.keyframes.size() < 1)
                throw std::runtime_error("effect parameter tracks require keyframes");
            for (const auto& key : track.keyframes) {
                if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                    key.value.front() < definition->minimum || key.value.front() > definition->maximum)
                    throw std::runtime_error("effect parameter keyframe is outside the declared parameter range: " + effect_id + "." + param);
            }
            const auto duplicate = std::find_if(layer.effect_param_tracks.begin(), layer.effect_param_tracks.end(),
                [&](const LayerPlan::EffectParamTrackPlan& existing) {
                    return existing.effect_id == effect_id && existing.param == param;
                });
            if (duplicate != layer.effect_param_tracks.end())
                throw std::runtime_error("duplicate effect parameter track: " + effect_id + "." + param);
            layer.effect_param_tracks.push_back({effect_id, param, std::move(track)});
        }
    }
}



void decode_layer_layout(LayerPlan& layer, const nlohmann::json& value) {
    const auto parse_layout_vec2 = [](const nlohmann::json& raw, const char* key) {
        const auto& v = raw.at(key);
        if (!v.is_array() || v.size() != 2) throw std::runtime_error(std::string("layout.") + key + " must be [x,y]");
        std::array<float,2> result{v[0].get<float>(),v[1].get<float>()};
        if (!std::isfinite(result[0]) || !std::isfinite(result[1]) || result[0] < 0 || result[1] < 0)
            throw std::runtime_error(std::string("layout.") + key + " values must be finite and non-negative");
        return result;
    };
    if (value.contains("flow")) {
        const auto& raw=value.at("flow"); LayerPlan::FlowLayoutPlan p;
        p.group_id=raw.at("group_id").get<std::string>(); p.axis=raw.value("axis",p.axis);
        p.align=raw.value("align",p.align); p.justify=raw.value("justify",p.justify);
        p.gap=raw.value("gap",0.0f); p.wrap=raw.value("wrap",false);
        if (raw.contains("cell_size")) p.cell_size=parse_layout_vec2(raw,"cell_size");
        if (raw.contains("stagger")) p.stagger=decode_stagger(raw.at("stagger"));
        static constexpr std::array<std::string_view,2> axes{"row","column"};
        static constexpr std::array<std::string_view,4> aligns{"start","center","end","stretch"};
        static constexpr std::array<std::string_view,6> justifies{"start","center","end","space_between","space_around","space_evenly"};
        if (p.group_id.empty() || p.group_id.size()>256 || !std::isfinite(p.gap) || p.gap<0 || p.gap>1000000 ||
            std::find(axes.begin(),axes.end(),p.axis)==axes.end() || std::find(aligns.begin(),aligns.end(),p.align)==aligns.end() || std::find(justifies.begin(),justifies.end(),p.justify)==justifies.end())
            throw std::runtime_error("flow layout has invalid group, axis, alignment, justification, or gap");
        layer.flow_layout=std::move(p);
    }
    if (value.contains("grid_layout")) {
        const auto& raw=value.at("grid_layout"); LayerPlan::GridLayoutPlan p;
        p.group_id=raw.at("group_id").get<std::string>(); p.fit=raw.value("fit",p.fit);
        p.columns=raw.value("columns",1); p.rows=raw.value("rows",1);
        if (raw.contains("gap")) p.gap=parse_layout_vec2(raw,"gap");
        if (raw.contains("cell_size")) p.cell_size=parse_layout_vec2(raw,"cell_size");
        if (raw.contains("stagger")) p.stagger=decode_stagger(raw.at("stagger"));
        if (p.group_id.empty() || p.group_id.size()>256 || p.columns<1 || p.columns>4096 || p.rows<1 || p.rows>4096 ||
            (p.fit!="auto" && p.fit!="fixed_columns" && p.fit!="fixed_rows"))
            throw std::runtime_error("grid layout has invalid group, dimensions, or fit");
        layer.grid_layout=std::move(p);
    }
    if (value.contains("stagger_group")) {
        const auto& raw = value.at("stagger_group");
        auto stagger = decode_stagger(raw);
        stagger.group_id = raw.at("group_id").get<std::string>();
        if (stagger.group_id.empty() || stagger.group_id.size() > 256)
            throw std::runtime_error("stagger_group group_id must contain 1..256 characters");
        layer.stagger_group = std::move(stagger);
    }
    if (layer.flow_layout && layer.grid_layout) throw std::runtime_error("a layer cannot belong to flow and grid layouts simultaneously");
 }

}  // namespace

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

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
    if (value.contains("inline_mesh")) {
        if (layer.type != LayerType::Mesh)
            throw std::runtime_error("inline_mesh is only valid for mesh layers");
        const auto& raw_mesh = value.at("inline_mesh");
        InlineMeshPlan mesh;
        if (raw_mesh.contains("primitive")) {
            if (raw_mesh.contains("vertices") || raw_mesh.contains("indices"))
                throw std::runtime_error("inline_mesh primitive and raw vertices/indices are mutually exclusive");
            const auto& raw = raw_mesh.at("primitive");
            if (!raw.is_object()) throw std::runtime_error("inline_mesh.primitive must be an object");
            static const std::unordered_set<std::string> primitive_fields{
                "kind", "width", "height", "depth", "radius", "radius_top", "radius_bottom", "tube",
                "width_segments", "height_segments", "radial_segments", "tubular_segments", "profile", "bevel"};
            InlineMeshPrimitivePlan primitive;
            primitive.kind = raw.value("kind", primitive.kind);
            const auto allowed_for_kind = [&]() -> std::unordered_set<std::string> {
                if (primitive.kind == "box") return {"kind", "width", "height", "depth"};
                if (primitive.kind == "plane") return {"kind", "width", "height", "width_segments", "height_segments"};
                if (primitive.kind == "sphere") return {"kind", "radius", "width_segments", "height_segments"};
                if (primitive.kind == "cylinder") return {"kind", "height", "radius_top", "radius_bottom", "radial_segments", "height_segments"};
                if (primitive.kind == "cone") return {"kind", "height", "radius_bottom", "radial_segments", "height_segments"};
                if (primitive.kind == "torus") return {"kind", "radius", "tube", "radial_segments", "tubular_segments"};
                if (primitive.kind == "extrude") return {"kind", "depth", "profile", "bevel"};
                return {"kind"};
            }();
            for (auto it = raw.begin(); it != raw.end(); ++it)
                if (!primitive_fields.contains(it.key()) || !allowed_for_kind.contains(it.key()))
                    throw std::runtime_error("inline_mesh.primitive has unknown or inapplicable field '" + it.key() + "'");
            primitive.width = raw.value("width", primitive.width);
            primitive.height = raw.value("height", primitive.height);
            primitive.depth = raw.value("depth", primitive.depth);
            primitive.radius = raw.value("radius", primitive.radius);
            primitive.radius_top = raw.value("radius_top", primitive.radius_top);
            primitive.radius_bottom = raw.value("radius_bottom", primitive.radius_bottom);
            primitive.tube = raw.value("tube", primitive.tube);
            primitive.width_segments = raw.value("width_segments", primitive.width_segments);
            primitive.height_segments = raw.value("height_segments", primitive.height_segments);
            primitive.radial_segments = raw.value("radial_segments", primitive.radial_segments);
            primitive.tubular_segments = raw.value("tubular_segments", primitive.tubular_segments);
            primitive.bevel = raw.value("bevel", primitive.bevel);
            if (primitive.kind == "extrude") {
                if (!raw.contains("profile") || !raw["profile"].is_array() ||
                    raw["profile"].size() < 3 || raw["profile"].size() > 4096)
                    throw std::runtime_error("inline_mesh extrusion profile must contain 3..4096 points");
                primitive.profile.reserve(raw["profile"].size());
                for (const auto& point : raw["profile"]) {
                    if (!point.is_array() || point.size() != 2 ||
                        !point[0].is_number() || !point[1].is_number())
                        throw std::runtime_error("inline_mesh extrusion profile points must be numeric pairs");
                    primitive.profile.push_back({point[0].get<float>(), point[1].get<float>()});
                }
            }
            mesh = expand_inline_mesh_primitive(primitive);
        } else {
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
        }
        if (raw_mesh.contains("material")) {
            const auto& raw_material = raw_mesh.at("material");
            InlineMeshMaterialPlan material;
            material.diffuse = raw_material.value("diffuse", material.diffuse);
            material.specular = raw_material.value("specular", material.specular);
            material.shininess = raw_material.value("shininess", material.shininess);
            material.rim_intensity = raw_material.value("rim_intensity", material.rim_intensity);
            if (raw_material.contains("base_color")) {
                const auto& color = raw_material.at("base_color");
                std::array<float, 4> decoded{};
                for (std::size_t component = 0; component < decoded.size(); ++component)
                    decoded[component] = color.at(component).get<float>();
                material.base_color = decoded;
            }
            if (!std::isfinite(material.diffuse) || material.diffuse < 0.0f || material.diffuse > 1.0f ||
                !std::isfinite(material.specular) || material.specular < 0.0f || material.specular > 1.0f ||
                !std::isfinite(material.shininess) || material.shininess < 1.0f || material.shininess > 1024.0f ||
                !std::isfinite(material.rim_intensity) || material.rim_intensity < 0.0f || material.rim_intensity > 16.0f)
                throw std::runtime_error("inline_mesh.material values are outside their supported ranges");
            if (material.base_color && !std::all_of(material.base_color->begin(), material.base_color->end(),
                    [](float component) { return std::isfinite(component) && component >= 0.0f && component <= 1.0f; }))
                throw std::runtime_error("inline_mesh.material.base_color components must be finite and in [0, 1]");
            mesh.material = material;
        }
        layer.inline_mesh = std::move(mesh);
    } else if (layer.type == LayerType::Mesh) {
        throw std::runtime_error("mesh layers require inline_mesh geometry");
    }
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
    if (value.contains("spans")) {
        if (layer.type != LayerType::Text)
            throw std::runtime_error("layer.spans is only valid for text layers");
        if (!value.at("spans").is_array() || value.at("spans").size() > 65536)
            throw std::runtime_error("layer.spans must be an array of at most 65536 entries");
        std::size_t previous_end = 0;
        std::vector<std::string> semantic_ids;
        const auto boundary = [&](std::size_t offset) {
            return offset == 0 || offset == layer.text.size() ||
                (static_cast<unsigned char>(layer.text[offset]) & 0xc0U) != 0x80U;
        };
        for (const auto& raw_span : value.at("spans")) {
            if (!raw_span.is_object())
                throw std::runtime_error("each text span must be an object");
            for (auto field = raw_span.begin(); field != raw_span.end(); ++field)
                if (field.key() != "start" && field.key() != "end" &&
                    field.key() != "semantic_id" && field.key() != "style")
                    throw std::runtime_error("text span contains unsupported field '" + field.key() + "'");
            const auto validOffset = [&](const nlohmann::json& offset) {
                if (!offset.is_number_integer()) return false;
                if (offset.is_number_unsigned())
                    return offset.get<std::uint64_t>() <= layer.text.size();
                const auto signed_offset = offset.get<std::int64_t>();
                return signed_offset >= 0 && static_cast<std::uint64_t>(signed_offset) <= layer.text.size();
            };
            if (!raw_span.contains("start") || !validOffset(raw_span.at("start")) ||
                !raw_span.contains("end") || !validOffset(raw_span.at("end")))
                throw std::runtime_error("text span start/end must be non-negative byte offsets within text");
            TextSpanOverride span;
            span.byte_start = raw_span.at("start").get<std::size_t>();
            span.byte_end = raw_span.at("end").get<std::size_t>();
            if (span.byte_start < previous_end || span.byte_start >= span.byte_end ||
                span.byte_end > layer.text.size() || !boundary(span.byte_start) || !boundary(span.byte_end))
                throw std::runtime_error("text spans must be sorted, non-overlapping UTF-8 byte ranges within text");
            previous_end = span.byte_end;
            if (raw_span.contains("semantic_id")) {
                if (!raw_span.at("semantic_id").is_string())
                    throw std::runtime_error("text span semantic_id must be a string");
                span.semantic_id = raw_span.at("semantic_id").get<std::string>();
                if (span.semantic_id->empty() || span.semantic_id->size() > 256)
                    throw std::runtime_error("text span semantic_id must contain 1..256 bytes");
                if (std::find(semantic_ids.begin(), semantic_ids.end(), *span.semantic_id) != semantic_ids.end())
                    throw std::runtime_error("text span semantic_id values must be unique per layer");
                semantic_ids.push_back(*span.semantic_id);
            }
            const auto styleIt = raw_span.find("style");
            if (styleIt != raw_span.end()) {
                const auto& style = *styleIt;
                if (!style.is_object() || style.empty())
                    throw std::runtime_error("text span style must be a non-empty object when present");
                for (auto field = style.begin(); field != style.end(); ++field)
                    if (field.key() != "color" && field.key() != "font_size_mult" &&
                        field.key() != "tracking" && field.key() != "stroke" && field.key() != "highlight")
                        throw std::runtime_error("text span style contains unsupported field '" + field.key() + "'");
                if (style.contains("color")) {
                    if (!style.at("color").is_string())
                        throw std::runtime_error("text span color must be a hex string");
                    const auto color = decode_text_span_color(style.at("color").get<std::string>());
                    if (!color) throw std::runtime_error("text span has invalid color");
                    span.color = *color;
                }
                if (style.contains("font_size_mult")) {
                    if (!style.at("font_size_mult").is_number())
                        throw std::runtime_error("text span font_size_mult must be a number");
                    const float mult = style.at("font_size_mult").get<float>();
                    if (!std::isfinite(mult) || mult <= 0.0f || mult > 100.0f)
                        throw std::runtime_error("text span font_size_mult must be finite and in (0, 100]");
                    span.font_size_multiplier = mult;
                }
                if (style.contains("tracking")) {
                    if (!style.at("tracking").is_number())
                        throw std::runtime_error("text span tracking must be a number");
                    const float tracking = style.at("tracking").get<float>();
                    if (!std::isfinite(tracking) || tracking < -1000.0f || tracking > 1000.0f)
                        throw std::runtime_error("text span tracking must be finite in [-1000, 1000]");
                    span.tracking = tracking;
                }
                if (style.contains("stroke")) {
                    const auto& raw_stroke = style.at("stroke");
                    if (!raw_stroke.is_object() || raw_stroke.empty())
                        throw std::runtime_error("text span stroke must be a non-empty object");
                    for (auto field = raw_stroke.begin(); field != raw_stroke.end(); ++field)
                        if (field.key() != "color" && field.key() != "width_em")
                            throw std::runtime_error("text span stroke contains unsupported field '" + field.key() + "'");
                    TextStrokeStyle stroke;
                    if (raw_stroke.contains("color")) {
                        if (!raw_stroke.at("color").is_string())
                            throw std::runtime_error("text span stroke color must be a hex string");
                        const auto color = decode_text_span_color(raw_stroke.at("color").get<std::string>());
                        if (!color) throw std::runtime_error("text span has invalid stroke color");
                        stroke.color = *color;
                    }
                    if (raw_stroke.contains("width_em")) {
                        if (!raw_stroke.at("width_em").is_number())
                            throw std::runtime_error("text span stroke width_em must be a number");
                        stroke.width_em = raw_stroke.at("width_em").get<float>();
                    }
                    if (!std::isfinite(stroke.width_em) || stroke.width_em < 0.0f || stroke.width_em > 100.0f)
                        throw std::runtime_error("text span stroke width_em must be finite and in [0, 100]");
                    span.stroke = stroke;
                }
                if (style.contains("highlight")) {
                    const auto& raw_highlight = style.at("highlight");
                    if (!raw_highlight.is_object() || raw_highlight.empty())
                        throw std::runtime_error("text span highlight must be a non-empty object");
                    for (auto field = raw_highlight.begin(); field != raw_highlight.end(); ++field)
                        if (field.key() != "background" && field.key() != "padding_em" && field.key() != "radius_em")
                            throw std::runtime_error("text span highlight contains unsupported field '" + field.key() + "'");
                    TextHighlightStyle highlight;
                    if (raw_highlight.contains("background")) {
                        if (!raw_highlight.at("background").is_string())
                            throw std::runtime_error("text span highlight background must be a hex string");
                        const auto color = decode_text_span_color(
                            raw_highlight.at("background").get<std::string>());
                        if (!color) throw std::runtime_error("text span has invalid highlight background color");
                        highlight.background = *color;
                    }
                    if (raw_highlight.contains("padding_em")) {
                        if (!raw_highlight.at("padding_em").is_number())
                            throw std::runtime_error("text highlight padding_em must be a number");
                        highlight.padding_em = raw_highlight.at("padding_em").get<float>();
                    }
                    if (raw_highlight.contains("radius_em")) {
                        if (!raw_highlight.at("radius_em").is_number())
                            throw std::runtime_error("text highlight radius_em must be a number");
                        highlight.radius_em = raw_highlight.at("radius_em").get<float>();
                    }
                    if ((highlight.padding_em && (!std::isfinite(*highlight.padding_em) ||
                            *highlight.padding_em < 0.0f || *highlight.padding_em > 100.0f)) ||
                        (highlight.radius_em && (!std::isfinite(*highlight.radius_em) ||
                            *highlight.radius_em < 0.0f || *highlight.radius_em > 100.0f)))
                        throw std::runtime_error("text highlight padding_em and radius_em must be finite in [0, 100]");
                    span.highlight = highlight;
                }
            }
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

    decode_shape_and_fill_gradient(layer, value);

    decode_layer_masks(layer, value);
    decode_layer_effects(layer, value);
    if (layer.type == LayerType::Mesh) {
        for (const auto& effect : layer.effects) {
            const auto& id = effect.canonical_effect_id;
            if (id != chronon3d::effects::ids::ColorTint &&
                id != chronon3d::effects::ids::LightGlow &&
                id != chronon3d::effects::ids::LightBloom &&
                id != chronon3d::effects::ids::GenerateNoise)
                throw std::runtime_error("mesh layers support only color.tint, light.glow, light.bloom, and generate.noise effects");
        }
    }
    decode_background_look(layer, value);
    if (value.contains("backdrop_filter")) {
        const auto& raw_bf = value.at("backdrop_filter");
        BackdropFilterPlan filter;
        filter.blur = raw_bf.value("blur", filter.blur);
        filter.brightness = raw_bf.value("brightness", filter.brightness);
        filter.saturation = raw_bf.value("saturation", filter.saturation);
        filter.refraction_strength = raw_bf.value("refraction_strength", filter.refraction_strength);
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
