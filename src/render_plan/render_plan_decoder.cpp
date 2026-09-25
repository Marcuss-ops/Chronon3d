#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <exception>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>
#include <set>

namespace {

bool is_3d_property(const std::string& property) {
    // Closed 3D gate: these are the camera-backed layer properties. Other
    // primitive tracks remain valid in flat mode and must not opt in silently.
    return property == "position_z" || property == "rotation_x" ||
           property == "rotation_y";
}

std::optional<std::string> find_3d_track(const nlohmann::json& layer) {
    const auto inspect_tracks = [](const nlohmann::json& tracks)
        -> std::optional<std::string> {
        if (!tracks.is_array()) return std::nullopt;
        for (const auto& track : tracks) {
            if (track.is_object() && track.contains("property") &&
                is_3d_property(track.at("property").get<std::string>())) {
                return track.at("property").get<std::string>();
            }
        }
        return std::nullopt;
    };

    if (layer.contains("animation") && layer.at("animation").is_object() &&
        layer.at("animation").contains("tracks")) {
        if (auto property = inspect_tracks(layer.at("animation").at("tracks")))
            return property;
    }
    if (layer.contains("text_animators") && layer.at("text_animators").is_array()) {
        for (const auto& animator : layer.at("text_animators")) {
            if (animator.is_object() && animator.contains("properties")) {
                if (auto property = inspect_tracks(animator.at("properties")))
                    return property;
            }
        }
    }
    return std::nullopt;
}

bool has_explicit_3d_vector(const nlohmann::json& layer, const char* key) {
    return layer.contains(key) && layer.at(key).is_array() &&
           layer.at(key).size() >= 3;
}

struct ResolvedVariables {
    nlohmann::json plan;
    std::vector<std::string> unused;
};

ResolvedVariables resolve_variables(const nlohmann::json& source) {
    ResolvedVariables result{source, {}};
    if (!source.contains("variables")) return result;
    const auto& raw_variables = source.at("variables");
    if (!raw_variables.is_object() || raw_variables.size() > 1024)
        throw std::runtime_error("variables must be an object with at most 1024 entries");
    std::unordered_map<std::string, nlohmann::json> variables;
    for (auto it = raw_variables.begin(); it != raw_variables.end(); ++it) {
        if (it.key().empty() ||
            !(std::isalpha(static_cast<unsigned char>(it.key().front())) || it.key().front() == '_') ||
            !std::all_of(it.key().begin() + 1, it.key().end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_';
            }) ||
            !(it.value().is_string() || it.value().is_number()))
            throw std::runtime_error("variables must map names to string or number values");
        variables.emplace(it.key(), it.value());
    }
    std::set<std::string> used;
    const auto resolve_string = [&](std::string value, bool typed,
                                    const std::string& path) -> nlohmann::json {
        std::optional<nlohmann::json> exact;
        std::string output;
        std::size_t cursor = 0;
        while (true) {
            const auto begin = value.find("${", cursor);
            if (begin == std::string::npos) {
                output.append(value, cursor, std::string::npos);
                break;
            }
            output.append(value, cursor, begin - cursor);
            const auto end = value.find('}', begin + 2);
            if (end == std::string::npos)
                throw std::runtime_error("unterminated variable reference in " + path);
            const auto name = value.substr(begin + 2, end - begin - 2);
            const auto found = variables.find(name);
            if (found == variables.end())
                throw std::runtime_error("unknown render variable '${" + name + "}' in " + path);
            used.insert(name);
            if (typed && begin == 0 && end + 1 == value.size()) {
                exact = found->second;
                break;
            }
            output += found->second.is_string() ? found->second.get<std::string>()
                                                : found->second.dump();
            cursor = end + 1;
        }
        if (exact) return *exact;
        return output;
    };
    const auto walk = [&](const auto& self, nlohmann::json& value,
                          const std::string& path, const std::string& parent_key) -> void {
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (it.key().find("${") != std::string::npos)
                    throw std::runtime_error("variable references are not allowed in object keys");
                self(self, it.value(), path + "." + it.key(), it.key());
            }
        } else if (value.is_array()) {
            for (std::size_t i = 0; i < value.size(); ++i)
                self(self, value[i], path + "[" + std::to_string(i) + "]", parent_key);
        } else if (value.is_string()) {
            const auto text = value.get<std::string>();
            if (text.find("${") != std::string::npos) {
                if (parent_key == "property" || parent_key == "type")
                    throw std::runtime_error("render variables are not allowed in property/type names");
                value = resolve_string(text, true, path);
            }
        }
    };
    result.plan.erase("variables");
    walk(walk, result.plan, "$", {});
    for (const auto& [name, unused] : variables) {
        (void)unused;
        if (!used.contains(name)) result.unused.push_back(name);
    }
    return result;
}

}  // namespace

namespace chronon3d::render_plan {

std::optional<PlanDecodeError> validate_render_plan_layer_relations(
    const RenderPlan& plan) {
    const bool has_v3_relations = std::any_of(
        plan.layers.begin(), plan.layers.end(), [](const LayerPlan& layer) {
            return layer.parent.has_value() || layer.track_matte.has_value() ||
                   layer.time_remap.has_value();
        });
    if (has_v3_relations && plan.schema != kRenderPlanSchemaV3) {
        return PlanDecodeError{
            "layers",
            "parent, track_matte, and time_remap are available only in render-plan V3",
            "v3_layer_feature_in_v2", "render-plan-relations"};
    }
    // Keep direct V2 RenderPlan inputs compatible with the historical API:
    // relation/name checks are new V3 semantics, not retroactive V2 rules.
    if (plan.schema != kRenderPlanSchemaV3) return std::nullopt;

    std::unordered_map<std::string, std::size_t> by_id;
    by_id.reserve(plan.layers.size());
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& id = plan.layers[index].id;
        if (!by_id.emplace(id, index).second) {
            return PlanDecodeError{
                "layers[" + std::to_string(index) + "].id",
                "layer ids must be unique",
                "duplicate_layer_id", "render-plan-relations"};
        }
    }

    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        if (layer.time_remap) {
            if (layer.type != LayerType::Video) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap",
                    "time_remap is supported only for video layers",
                    "time_remap_requires_video", "render-plan-relations"};
            }
            if (layer.time_remap->speed &&
                (!std::isfinite(*layer.time_remap->speed) ||
                 *layer.time_remap->speed <= 0.0f ||
                 *layer.time_remap->speed > 1000.0f)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap.speed",
                    "speed must be finite and in (0, 1000]",
                    "invalid_time_remap_speed", "render-plan-relations"};
            }
            if (layer.time_remap->freeze_frame &&
                (layer.time_remap->freeze_frame->integral() < 0 ||
                 layer.time_remap->freeze_frame->integral() > 1000000)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap.freeze_frame",
                    "freeze frame must be in [0, 1000000]",
                    "invalid_time_remap_freeze_frame", "render-plan-relations"};
            }
            if (layer.time_remap->freeze_frame && layer.time_remap->animation) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap",
                    "freeze_frame and animation cannot both be specified",
                    "time_remap_freeze_animation_conflict", "render-plan-relations"};
            }
            if (layer.time_remap->animation) {
                const auto& track = *layer.time_remap->animation;
                if (track.keyframes.empty()) {
                    return PlanDecodeError{
                        "layers[" + std::to_string(index) + "].time_remap.animation.keyframes",
                        "time-remap animation requires at least one keyframe",
                        "time_remap_animation_empty", "render-plan-relations"};
                }
                for (const auto& key : track.keyframes) {
                    if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                        key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < 0.0f || key.value.front() > 1000000.0f) {
                        return PlanDecodeError{
                            "layers[" + std::to_string(index) + "].time_remap.animation.keyframes",
                            "time-remap keyframes require bounded non-negative frames and scalar values",
                            "invalid_time_remap_keyframe", "render-plan-relations"};
                    }
                }
            }
        }
        if (layer.parent) {
            if (*layer.parent == layer.id) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].parent",
                    "a layer cannot parent itself",
                    "layer_parent_self_reference", "render-plan-relations"};
            }
            if (!by_id.contains(*layer.parent)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].parent",
                    "parent layer id does not exist: " + *layer.parent,
                    "layer_parent_missing", "render-plan-relations"};
            }
        }
        if (layer.track_matte) {
            if (layer.track_matte->source == layer.id) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].track_matte.source",
                    "a layer cannot use itself as a track-matte source",
                    "track_matte_self_reference", "render-plan-relations"};
            }
            if (!by_id.contains(layer.track_matte->source)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].track_matte.source",
                    "track-matte source layer id does not exist: " + layer.track_matte->source,
                    "track_matte_source_missing", "render-plan-relations"};
            }
        }
    }

    std::vector<std::uint8_t> state(plan.layers.size(), 0);
    const auto visit = [&](const auto& self, std::size_t index) -> bool {
        if (state[index] == 1) return false;
        if (state[index] == 2) return true;
        state[index] = 1;
        if (plan.layers[index].parent) {
            const auto parent = by_id.at(*plan.layers[index].parent);
            if (!self(self, parent)) return false;
        }
        state[index] = 2;
        return true;
    };
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        if (!visit(visit, index)) {
            return PlanDecodeError{
                "layers[" + std::to_string(index) + "].parent",
                "layer parent relationships must be acyclic",
                "layer_parent_cycle", "render-plan-relations"};
        }
    }
    return std::nullopt;
}

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    ResolvedVariables resolved;
    try {
        resolved = resolve_variables(root);
    } catch (const std::exception& error) {
        return PlanDecodeError{"variables", error.what(), "invalid_render_variables", "render-plan-variables"};
    }
    const auto& resolved_root = resolved.plan;
    const auto validation = validate_render_plan(resolved_root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        RenderPlan plan;
        plan.warnings.reserve(resolved.unused.size());
        for (const auto& name : resolved.unused)
            plan.warnings.push_back("unused render variable '" + name + "'");
        plan.schema = resolved_root.at("schema").get<std::string>();
        plan.job_id = resolved_root.value("job_id", std::string{"chronon_plan"});
        const auto& canvas = resolved_root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            FrameRate{canvas.at("fps_num").get<int>(), canvas.at("fps_den").get<int>()},
            Frame{canvas.at("duration_frames").get<std::int64_t>()}};
        if (resolved_root.contains("temporal_motion_blur")) {
            const auto& raw = resolved_root.at("temporal_motion_blur");
            MotionBlurPlan blur;
            blur.shutter_angle = raw.value("shutter_angle", 180.0f);
            blur.samples = raw.value("samples", std::uint32_t{12});
            if (!std::isfinite(blur.shutter_angle) || blur.shutter_angle <= 0.0f ||
                blur.shutter_angle > 360.0f || blur.samples < 2 || blur.samples > 64)
                return PlanDecodeError{
                    "temporal_motion_blur",
                    "requires shutter_angle in (0, 360] and samples in [2, 64]",
                    "invalid_temporal_motion_blur", "render-plan-decoder"};
            if (std::any_of(resolved_root.at("layers").begin(),
                            resolved_root.at("layers").end(), [](const auto& layer) {
                                return layer.is_object() && layer.contains("motion_blur");
                            }))
                return PlanDecodeError{
                    "temporal_motion_blur",
                    "cannot be combined with per-layer analytic motion_blur",
                    "conflicting_motion_blur_modes", "render-plan-decoder"};
            plan.temporal_motion_blur = blur;
        }
        if (resolved_root.contains("camera"))
            plan.camera = detail::decode_camera(resolved_root.at("camera"));
        if (resolved_root.contains("camera_animation")) {
            if (!plan.camera)
                return PlanDecodeError{"camera_animation",
                    "camera_animation requires a camera object",
                    "camera_animation_requires_camera", "render-plan-decoder"};
            plan.camera_animation = detail::decode_camera_tracks(resolved_root.at("camera_animation"));
        }

        std::size_t layer_index = 0;
        for (const auto& layer_value : resolved_root.at("layers")) {
            auto layer = detail::decode_layer(layer_value);
            if (layer.safe_insets &&
                ((*layer.safe_insets)[0] + (*layer.safe_insets)[2] >=
                     static_cast<float>(plan.canvas.height) ||
                 (*layer.safe_insets)[1] + (*layer.safe_insets)[3] >=
                     static_cast<float>(plan.canvas.width))) {
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "].safe_insets",
                    "safe insets must leave a non-empty canvas area",
                    "invalid_safe_insets", "render-plan-decoder"};
            }
            const bool explicit_3d_vector =
                has_explicit_3d_vector(layer_value, "position") ||
                has_explicit_3d_vector(layer_value, "scale") ||
                has_explicit_3d_vector(layer_value, "rotation");
            const auto offending_track = find_3d_track(layer_value);
            if (layer.depth_role && has_explicit_3d_vector(layer_value, "position") &&
                std::abs(layer.position[2]) > 0.0f) {
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "].depth_role",
                    "use depth_role + depth_offset, or explicit position_z, not both",
                    "depth_role_position_conflict", "render-plan-decoder"};
            }
            if (!layer.enable_3d && (explicit_3d_vector || offending_track)) {
                std::string reason = "3D transform data requires enable_3d=true";
                if (offending_track) {
                    reason += "; offending track: " + *offending_track;
                }
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "].enable_3d",
                    std::move(reason),
                    "3d_requires_explicit_enable",
                    "render-plan-decoder"};
            }
            if (detail::invalid_logical_path(layer.asset)) {
                return PlanDecodeError{"layers[].asset",
                    "asset references must be relative logical paths"};
            }
            if (detail::invalid_logical_path(layer.source)) {
                return PlanDecodeError{"layers[].source",
                    "source references must be relative logical paths"};
            }
            if (detail::invalid_logical_path(layer.font)) {
                return PlanDecodeError{"layers[].style.font",
                    "font references must be relative logical paths"};
            }
            if (layer.type == LayerType::Text) {
                if (!layer.style) {
                    return PlanDecodeError{"layers[].style",
                        "text layers require a concrete style object"};
                }
                if (layer.font.empty()) {
                    return PlanDecodeError{"layers[].style.font",
                        "text layers require an explicit prepared font asset"};
                }
                if (!layer.style->font_size || *layer.style->font_size <= 0.0f) {
                    return PlanDecodeError{"layers[].style.font_size",
                        "text layers require a positive concrete font_size"};
                }
                if (layer.style->fill.empty()) {
                    return PlanDecodeError{"layers[].style.fill",
                        "text layers require a concrete fill color"};
                }
            }
            plan.layers.push_back(std::move(layer));
            ++layer_index;
        }

        if (const auto relation_error = validate_render_plan_layer_relations(plan))
            return *relation_error;

        const auto& output = resolved_root.at("output");
        plan.output.path = output.at("path").get<std::string>();
        if (output.contains("format"))
            plan.output.format = detail::output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            plan.output.codec = detail::video_codec(output.at("codec").get<std::string>());
        plan.output.bitrate = output.value("bitrate", std::int64_t{0});
        plan.output.crf = output.value("crf", 0);
        plan.output.qp = output.value("qp", -1);
        const auto rate_control = output.value("rate_control", "crf");
        if (rate_control == "qp") plan.output.rate_control = RateControlMode::ConstantQp;
        else if (rate_control == "bitrate") plan.output.rate_control = RateControlMode::Bitrate;
        else if (rate_control == "crf") plan.output.rate_control = RateControlMode::Crf;
        else return PlanDecodeError{"output.rate_control", "must be crf, qp, or bitrate"};
        plan.output.profile_id = output.value("profile_id", std::string{});
        if (plan.output.profile_id == "velox-h264-1080p30-v1" &&
            (plan.canvas.width != 1920 || plan.canvas.height != 1080 ||
             plan.canvas.fps != FrameRate{30, 1})) {
            return PlanDecodeError{"canvas", "velox-h264-1080p30-v1 requires 1920x1080 at 30 fps"};
        }
        if (resolved_root.contains("budget")) {
            const auto& budget = resolved_root.at("budget");
            plan.budget.max_temporal_pixels = budget.value(
                "max_temporal_pixels", plan.budget.max_temporal_pixels);
        }
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;
        plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
