#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cmath>
#include <exception>
#include <string>
#include <utility>

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

}  // namespace

namespace chronon3d::render_plan {

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    const auto validation = validate_render_plan(root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        RenderPlan plan;
        plan.schema = root.at("schema").get<std::string>();
        plan.job_id = root.value("job_id", std::string{"chronon_plan"});
        const auto& canvas = root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            FrameRate{canvas.at("fps_num").get<int>(), canvas.at("fps_den").get<int>()},
            Frame{canvas.at("duration_frames").get<std::int64_t>()}};
        if (root.contains("camera"))
            plan.camera = detail::decode_camera(root.at("camera"));
        if (root.contains("camera_animation")) {
            if (!plan.camera)
                return PlanDecodeError{"camera_animation",
                    "camera_animation requires a camera object",
                    "camera_animation_requires_camera", "render-plan-decoder"};
            plan.camera_animation = detail::decode_camera_tracks(root.at("camera_animation"));
        }

        std::size_t layer_index = 0;
        for (const auto& layer_value : root.at("layers")) {
            auto layer = detail::decode_layer(layer_value);
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

        const auto& output = root.at("output");
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
        if (root.contains("budget")) {
            const auto& budget = root.at("budget");
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
