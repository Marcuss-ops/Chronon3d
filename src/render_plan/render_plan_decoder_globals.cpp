#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

std::optional<PlanDecodeError> decode_plan_globals(
    const nlohmann::json& root, RenderPlan& plan) {
if (root.contains("color_management")) {
    const auto& raw = root.at("color_management");
    RenderPlan::ColorManagementPlan cm;
    cm.ocio_config = raw.at("ocio_config").get<std::string>();
    cm.working_space = raw.at("working_space").get<std::string>();
    cm.video_input_space = raw.value("video_input_space", std::string{});
    cm.output_display = raw.value("output_display", std::string{});
    cm.output_view = raw.value("output_view", std::string{});
    if (cm.output_display.empty() != cm.output_view.empty())
        return PlanDecodeError{"color_management",
            "output_display and output_view must be specified together",
            "incomplete_ocio_output_view", "render-plan-color"};
    if (cm.ocio_config.empty() || cm.working_space.empty())
        return PlanDecodeError{"color_management",
            "ocio_config and working_space must be non-empty",
            "invalid_color_management", "render-plan-color"};
    if (detail::invalid_logical_path(cm.ocio_config))
        return PlanDecodeError{"color_management.ocio_config",
            "OCIO config must be a relative path inside the mounted asset root",
            "invalid_ocio_config_path", "render-plan-color"};
    plan.color_management = std::move(cm);
}
const auto& canvas = root.at("canvas");
plan.canvas = CanvasSpec{
    canvas.at("width").get<int>(),
    canvas.at("height").get<int>(),
    FrameRate{canvas.at("fps_num").get<int>(), canvas.at("fps_den").get<int>()},
    Frame{canvas.at("duration_frames").get<std::int64_t>()}};
if (root.contains("audio")) {
    auto decoded_audio = detail::decode_audio_mix(
        root.at("audio"), plan.canvas.duration);
    if (!decoded_audio) return decoded_audio.error();
    plan.audio = std::move(decoded_audio.value().mix);
    plan.warnings.insert(plan.warnings.end(),
        std::make_move_iterator(decoded_audio.value().warnings.begin()),
        std::make_move_iterator(decoded_audio.value().warnings.end()));
}
if (root.contains("temporal_motion_blur")) {
    const auto& raw = root.at("temporal_motion_blur");
    MotionBlurPlan blur;
    blur.shutter_angle = raw.value("shutter_angle", 180.0f);
    blur.samples = raw.value("samples", std::uint32_t{12});
    if (!std::isfinite(blur.shutter_angle) || blur.shutter_angle <= 0.0f ||
        blur.shutter_angle > 360.0f || blur.samples < 2 || blur.samples > 64)
        return PlanDecodeError{
            "temporal_motion_blur",
            "requires shutter_angle in (0, 360] and samples in [2, 64]",
            "invalid_temporal_motion_blur", "render-plan-decoder"};
    if (std::any_of(root.at("layers").begin(),
                    root.at("layers").end(), [](const auto& layer) {
                        return layer.is_object() && layer.contains("motion_blur");
                    }))
        return PlanDecodeError{
            "temporal_motion_blur",
            "cannot be combined with per-layer analytic motion_blur",
            "conflicting_motion_blur_modes", "render-plan-decoder"};
    plan.temporal_motion_blur = blur;
}
if (root.contains("camera"))
    plan.camera = detail::decode_camera(root.at("camera"));
if (root.contains("camera_shake"))
    return PlanDecodeError{"camera_shake",
    "camera_shake is no longer a RenderPlan feature; author it in ChrononMotion3D",
        "camera_shake_must_be_baked_by_motion", "render-plan-camera"};
if (root.contains("lighting_rig")) {
    if (plan.schema != kRenderPlanSchemaV3)
        return PlanDecodeError{"lighting_rig", "lighting_rig is available only in render-plan V3", "v3_lighting_in_v2", "render-plan-lighting"};
    const auto& raw = root.at("lighting_rig");
    LightingRigPlan rig;
    const auto read_array = [&](const char* key, auto& target) {
        if (!raw.contains(key)) return;
        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] = raw.at(key).at(i).get<float>();
    };
    const auto read_color = [&](const char* key, auto& target) {
        if (!raw.contains(key)) return;
        const auto& color = raw.at(key);
        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] = color.at(i).get<float>();
    };
    read_array("key_direction", rig.key_direction);
    read_color("key_color", rig.key_color);
    read_color("ambient_color", rig.ambient_color);
    read_color("rim_color", rig.rim_color);
    rig.key_intensity = raw.value("key_intensity", rig.key_intensity);
    rig.ambient_intensity = raw.value("ambient_intensity", rig.ambient_intensity);
    rig.rim_enabled = raw.value("rim_enabled", rig.rim_enabled);
    rig.rim_intensity = raw.value("rim_intensity", rig.rim_intensity);
    rig.rim_power = raw.value("rim_power", rig.rim_power);
    const auto finite_range = [](float value, float low, float high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    if (!std::all_of(rig.key_direction.begin(), rig.key_direction.end(),
            [](float value) { return std::isfinite(value) && std::abs(value) <= 1.0f; }) ||
        std::sqrt(rig.key_direction[0] * rig.key_direction[0] + rig.key_direction[1] * rig.key_direction[1] + rig.key_direction[2] * rig.key_direction[2]) < 1e-6f ||
        !std::all_of(rig.key_color.begin(), rig.key_color.end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }) ||
        !std::all_of(rig.ambient_color.begin(), rig.ambient_color.end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }) ||
        !std::all_of(rig.rim_color.begin(), rig.rim_color.end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }) ||
        !finite_range(rig.key_intensity, 0.0f, 16.0f) ||
        !finite_range(rig.ambient_intensity, 0.0f, 16.0f) ||
        !finite_range(rig.rim_intensity, 0.0f, 16.0f) ||
        !finite_range(rig.rim_power, 1e-6f, 32.0f))
        return PlanDecodeError{"lighting_rig", "lighting rig values are outside their supported finite ranges", "invalid_lighting_rig", "render-plan-lighting"};
    plan.lighting_rig = rig;
}
if (root.contains("environment_light")) {
    if (plan.schema != kRenderPlanSchemaV3)
        return PlanDecodeError{"environment_light", "environment_light is available only in render-plan V3",
            "v3_environment_light_in_v2", "render-plan-lighting"};
    const auto& raw = root.at("environment_light");
    EnvironmentLightPlan environment;
    environment.probe_asset = raw.at("probe_asset").get<std::string>();
    environment.intensity = raw.value("intensity", 1.0f);
    environment.rotation_y = raw.value("rotation_y", 0.0f);
    if (environment.probe_asset.empty() || detail::invalid_logical_path(environment.probe_asset))
        return PlanDecodeError{"environment_light.probe_asset",
            "probe_asset must be a non-empty logical path inside the mounted asset root",
            "invalid_environment_probe_path", "render-plan-lighting"};
    if (raw.contains("irradiance_sh")) {
        const auto& coefficients = raw.at("irradiance_sh");
        if (!coefficients.is_array() || coefficients.size() != environment.irradiance_sh.size())
            return PlanDecodeError{"environment_light.irradiance_sh",
                "irradiance_sh must contain exactly 27 RGB-interleaved values",
                "invalid_environment_sh", "render-plan-lighting"};
        for (std::size_t i = 0; i < environment.irradiance_sh.size(); ++i)
            environment.irradiance_sh[i] = coefficients.at(i).get<float>();
    } else {
        return PlanDecodeError{"environment_light.irradiance_sh", "irradiance_sh is required",
            "missing_environment_sh", "render-plan-lighting"};
    }
    const auto finite_sh = std::all_of(environment.irradiance_sh.begin(), environment.irradiance_sh.end(),
        [](float value) { return std::isfinite(value) && std::abs(value) <= 1.0e6f; });
    if (!std::isfinite(environment.intensity) || environment.intensity < 0.f || environment.intensity > 16.f ||
        !std::isfinite(environment.rotation_y) || std::abs(environment.rotation_y) > 1.0e6f || !finite_sh)
        return PlanDecodeError{"environment_light", "environment light values are outside supported finite ranges",
            "invalid_environment_light", "render-plan-lighting"};
    if (raw.contains("rotation_animation")) {
        environment.rotation_animation = detail::decode_animation_track(raw.at("rotation_animation"));
        const auto& track = *environment.rotation_animation;
        if (track.property != "rotation_y" || track.keyframes.empty() || track.keyframes.size() > 1'000'000)
            return PlanDecodeError{"environment_light.rotation_animation",
                "rotation_animation must be a non-empty rotation_y track with at most 1000000 keys",
                "invalid_environment_rotation_animation", "render-plan-lighting"};
        std::int64_t previous = -1;
        for (std::size_t i = 0; i < track.keyframes.size(); ++i) {
            const auto& key = track.keyframes[i];
            const auto frame = key.frame.integral();
            if (key.value.size() != 1 || frame < 0 || frame >= plan.canvas.duration.integral() ||
                frame <= previous || !std::isfinite(key.value[0]) || std::abs(key.value[0]) > 1.0e6f)
                return PlanDecodeError{"environment_light.rotation_animation.keyframes[" + std::to_string(i) + "]",
                    "rotation keyframes must be ordered, within the composition, and carry one finite angle",
                    "invalid_environment_rotation_keyframe", "render-plan-lighting"};
            previous = frame;
        }
    }
    plan.environment_light = std::move(environment);
}
if (root.contains("camera_samples")) {
    if (!plan.camera)
        return PlanDecodeError{"camera_samples",
            "camera_samples requires a camera object for static renderer defaults",
            "camera_samples_requires_camera", "render-plan-decoder"};
    plan.camera_samples = detail::decode_camera_samples(root.at("camera_samples"));
}
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
