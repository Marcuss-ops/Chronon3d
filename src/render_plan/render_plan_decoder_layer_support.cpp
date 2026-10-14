#include "render_plan_decoder_detail.hpp"
#include "easing_contract.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
namespace chronon3d::render_plan::detail {
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
        if (!is_stagger_easing(plan.easing))
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

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}
void decode_layer_inline_mesh(LayerPlan& layer, const nlohmann::json& value) {
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
        if (raw_mesh.contains("animation")) {
            const auto& raw_animation = raw_mesh.at("animation");
            if (!raw_animation.is_array() || raw_animation.empty() ||
                raw_animation.size() > kInlineMeshMaxAnimationSamples)
                throw std::runtime_error("inline_mesh.animation must contain 1..4096 baked samples");
            std::int64_t previous_frame = -1;
            mesh.animation.reserve(raw_animation.size());
            for (const auto& raw_sample : raw_animation) {
                InlineMeshFrameSamplePlan sample;
                sample.frame = raw_sample.at("frame").get<std::int64_t>();
                const auto& positions = raw_sample.at("positions");
                if (sample.frame < 0 || sample.frame <= previous_frame || sample.frame > 1'000'000)
                    throw std::runtime_error("inline_mesh.animation frames must be strictly increasing and in [0, 1000000]");
                if (!positions.is_array() || positions.size() != mesh.vertices.size())
                    throw std::runtime_error("inline_mesh.animation positions must match the base vertex count");
                sample.positions.reserve(positions.size());
                for (const auto& raw_position : positions) {
                    std::array<float, 3> position{};
                    if (!raw_position.is_array() || raw_position.size() != 3)
                        throw std::runtime_error("inline_mesh.animation positions must be 3D vectors");
                    for (std::size_t component = 0; component < position.size(); ++component) {
                        position[component] = raw_position.at(component).get<float>();
                        if (!std::isfinite(position[component]) ||
                            std::abs(position[component]) > 1'000'000.0f)
                            throw std::runtime_error("inline_mesh.animation position exceeds finite bounds");
                    }
                    sample.positions.push_back(position);
                }
                previous_frame = sample.frame;
                mesh.animation.push_back(std::move(sample));
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

}
void decode_layer_text_spans(LayerPlan& layer, const nlohmann::json& value) {
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


}
} // namespace chronon3d::render_plan::detail
