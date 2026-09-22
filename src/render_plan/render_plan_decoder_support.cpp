#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <stdexcept>
#include <array>
#include <cmath>
#include <sstream>

namespace chronon3d::render_plan::detail {

bool invalid_logical_path(const std::string& value) {
    if (value.empty()) return false;
    const std::filesystem::path path{value};
    if (path.is_absolute()) return true;
    for (const auto& component : path) {
        if (component == std::filesystem::path("..")) return true;
    }
    return false;
}

LayerType layer_type(const std::string& value) {
    if (value == "image") return LayerType::Image;
    if (value == "video") return LayerType::Video;
    if (value == "text") return LayerType::Text;
    if (value == "color") return LayerType::Color;
    throw std::runtime_error("unsupported primitive layer type: " + value);
}

FitMode fit_mode(const std::string& value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
}

std::optional<chronon3d::BlendMode> blend_mode(const std::string& value) {
    if (value == "normal") return chronon3d::BlendMode::Normal;
    if (value == "add") return chronon3d::BlendMode::Add;
    if (value == "multiply") return chronon3d::BlendMode::Multiply;
    if (value == "screen") return chronon3d::BlendMode::Screen;
    if (value == "overlay") return chronon3d::BlendMode::Overlay;
    if (value == "darken") return chronon3d::BlendMode::Darken;
    if (value == "lighten") return chronon3d::BlendMode::Lighten;
    if (value == "difference") return chronon3d::BlendMode::Difference;
    if (value == "exclusion") return chronon3d::BlendMode::Exclusion;
    if (value == "soft_light") return chronon3d::BlendMode::SoftLight;
    if (value == "hard_light") return chronon3d::BlendMode::HardLight;
    if (value == "color_dodge") return chronon3d::BlendMode::ColorDodge;
    if (value == "color_burn") return chronon3d::BlendMode::ColorBurn;
    return std::nullopt;
}

OutputFormat output_format(const std::string& value) {
    if (value == "mp4") return OutputFormat::Mp4;
    if (value == "mkv") return OutputFormat::Mkv;
    if (value == "webm") return OutputFormat::WebM;
    return OutputFormat::Png;
}

VideoCodec video_codec(const std::string& value) {
    if (value == "h264") return VideoCodec::H264;
    if (value == "h265") return VideoCodec::H265;
    if (value == "vp9") return VideoCodec::VP9;
    if (value == "av1") return VideoCodec::AV1;
    return VideoCodec::Auto;
}

CameraPropertyPlan camera_property(const std::string& value) {
    if (value == "camera_position_x") return CameraPropertyPlan::PositionX;
    if (value == "camera_position_y") return CameraPropertyPlan::PositionY;
    if (value == "camera_position_z") return CameraPropertyPlan::PositionZ;
    if (value == "camera_rotation_x") return CameraPropertyPlan::RotationX;
    if (value == "camera_rotation_y") return CameraPropertyPlan::RotationY;
    if (value == "camera_rotation_z") return CameraPropertyPlan::RotationZ;
    if (value == "camera_fov_deg") return CameraPropertyPlan::FovDeg;
    if (value == "camera_zoom") return CameraPropertyPlan::Zoom;
    throw std::runtime_error(
        "unsupported camera animation property '" + value +
        "'; valid properties: camera_position_x, camera_position_y, camera_position_z, "
        "camera_rotation_x, camera_rotation_y, camera_rotation_z, camera_fov_deg, camera_zoom");
}

CameraPlan decode_camera(const nlohmann::json& value) {
    CameraPlan camera;
    camera.type = value.value("type", std::string{"perspective"});
    if (camera.type != "perspective" && camera.type != "orthographic")
        throw std::runtime_error("camera.type must be perspective or orthographic: " + camera.type);

    const auto read_vec3 = [](const nlohmann::json& object, const char* key,
                              std::array<float, 3>& result) {
        if (!object.contains(key)) return;
        const auto& array = object.at(key);
        if (!array.is_array() || array.size() != 3)
            throw std::runtime_error(std::string("camera.") + key + " must contain exactly 3 numbers");
        for (std::size_t i = 0; i < 3; ++i) result[i] = array.at(i).get<float>();
    };
    read_vec3(value, "position", camera.position);
    read_vec3(value, "rotation_deg", camera.rotation_deg);
    camera.fov_deg = value.value("fov_deg", 50.0f);
    camera.near = value.value("near", 1.0f);
    camera.far = value.value("far", 10000.0f);
    camera.zoom = value.value("zoom", 1.0f);
    if (!std::isfinite(camera.fov_deg) || camera.fov_deg < 10.0f || camera.fov_deg > 120.0f)
        throw std::runtime_error("camera.fov_deg must be finite and in [10, 120]");
    if (!std::isfinite(camera.near) || camera.near <= 0.0f)
        throw std::runtime_error("camera.near must be finite and > 0");
    if (!std::isfinite(camera.far) || camera.far <= camera.near)
        throw std::runtime_error("camera.far must be finite and > near");
    if (!std::isfinite(camera.zoom) || camera.zoom <= 0.0f)
        throw std::runtime_error("camera.zoom must be finite and > 0");
    return camera;
}

std::vector<CameraTrackPlan> decode_camera_tracks(const nlohmann::json& value) {
    if (!value.is_object() || !value.contains("tracks") || !value.at("tracks").is_array())
        throw std::runtime_error("camera_animation.tracks must be an array");
    std::vector<CameraTrackPlan> tracks;
    for (const auto& raw : value.at("tracks")) {
        CameraTrackPlan track;
        track.property = camera_property(raw.at("property").get<std::string>());
        track.easing = raw.value("easing", std::string{"linear"});
        for (const auto& raw_key : raw.at("keyframes")) {
            AnimationKeyframePlan key;
            key.frame = Frame{raw_key.at("frame").get<std::int64_t>()};
            const auto& raw_value = raw_key.at("value");
            if (!raw_value.is_number())
                throw std::runtime_error("camera animation keyframe values must be scalar numbers");
            key.value.push_back(raw_value.get<float>());
            track.keyframes.push_back(std::move(key));
        }
        tracks.push_back(std::move(track));
    }
    return tracks;
}

}  // namespace chronon3d::render_plan::detail
