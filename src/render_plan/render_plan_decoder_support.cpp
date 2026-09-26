#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <stdexcept>
#include <array>
#include <algorithm>
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

graphics::GradientDefinition decode_gradient_definition(
    const nlohmann::json& raw_gradient) {
    graphics::GradientDefinition gradient;
    const auto kind = raw_gradient.at("type").get<std::string>();
    if (kind == "linear") gradient.type = graphics::GradientType::Linear;
    else if (kind == "radial") gradient.type = graphics::GradientType::Radial;
    else if (kind == "conic") gradient.type = graphics::GradientType::Conic;
    else throw std::runtime_error("unsupported gradient.type: " + kind);

    const auto read_vec2 = [](const nlohmann::json& object,
                              const char* key, Vec2& output) {
        if (!object.contains(key)) return;
        const auto& raw = object.at(key);
        if (!raw.is_array() || raw.size() != 2)
            throw std::runtime_error(std::string("gradient.") + key + " must have two numbers");
        output = {raw[0].get<float>(), raw[1].get<float>()};
        if (!std::isfinite(output.x) || !std::isfinite(output.y))
            throw std::runtime_error(std::string("gradient.") + key + " must be finite");
    };
    read_vec2(raw_gradient, "start", gradient.start);
    read_vec2(raw_gradient, "end", gradient.end);
    read_vec2(raw_gradient, "center", gradient.center);
    gradient.radius = raw_gradient.value("radius", gradient.radius);
    gradient.angle = raw_gradient.value("angle", gradient.angle);
    if (!std::isfinite(gradient.radius) || gradient.radius <= 0.0f ||
        !std::isfinite(gradient.angle))
        throw std::runtime_error("gradient radius and angle must be finite; radius must be positive");

    const auto spread = raw_gradient.value("spread", std::string{"pad"});
    if (spread == "repeat") gradient.spread = graphics::GradientSpread::Repeat;
    else if (spread == "reflect") gradient.spread = graphics::GradientSpread::Reflect;
    else if (spread != "pad") throw std::runtime_error("unsupported gradient spread: " + spread);

    if (raw_gradient.at("color_stops").size() < 2)
        throw std::runtime_error("gradient requires at least two color stops");
    for (const auto& raw_stop : raw_gradient.at("color_stops")) {
        const auto& color = raw_stop.at("color");
        if (!color.is_array() || color.size() != 4)
            throw std::runtime_error("gradient color stop must have RGBA");
        const auto position = raw_stop.at("position").get<float>();
        const Color decoded_color{color[0].get<float>(), color[1].get<float>(),
                                  color[2].get<float>(), color[3].get<float>()};
        const auto in_unit_range = [](float component) {
            return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
        };
        if (!in_unit_range(position) || !in_unit_range(decoded_color.r) ||
            !in_unit_range(decoded_color.g) || !in_unit_range(decoded_color.b) ||
            !in_unit_range(decoded_color.a))
            throw std::runtime_error("gradient color stops must be finite and in [0, 1]");
        gradient.color_stops.push_back({position, decoded_color});
    }
    std::stable_sort(gradient.color_stops.begin(), gradient.color_stops.end(),
        [](const auto& a, const auto& b) { return a.position < b.position; });
    if (raw_gradient.contains("opacity_stops")) {
        for (const auto& stop : raw_gradient.at("opacity_stops")) {
            const auto position = stop.at("position").get<float>();
            const auto opacity = stop.at("opacity").get<float>();
            if (!std::isfinite(position) || position < 0.0f || position > 1.0f ||
                !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
                throw std::runtime_error("gradient opacity stops must be finite and in [0, 1]");
            gradient.opacity_stops.push_back({position, opacity});
        }
        std::stable_sort(gradient.opacity_stops.begin(), gradient.opacity_stops.end(),
            [](const auto& a, const auto& b) { return a.position < b.position; });
    }
    return gradient;
}

LayerType layer_type(const std::string& value) {
    if (value == "image") return LayerType::Image;
    if (value == "video") return LayerType::Video;
    if (value == "text") return LayerType::Text;
    if (value == "color") return LayerType::Color;
    if (value == "shape") return LayerType::Shape;
    if (value == "particles") return LayerType::Particles;
    if (value == "light") return LayerType::Light;
    if (value == "adjustment") return LayerType::Adjustment;
    if (value == "mesh") return LayerType::Mesh;
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
    if (value == "subtract") return chronon3d::BlendMode::Subtract;
    if (value == "divide") return chronon3d::BlendMode::Divide;
    if (value == "vivid_light") return chronon3d::BlendMode::VividLight;
    if (value == "pin_light") return chronon3d::BlendMode::PinLight;
    if (value == "linear_light") return chronon3d::BlendMode::LinearLight;
    if (value == "hue") return chronon3d::BlendMode::Hue;
    if (value == "saturation") return chronon3d::BlendMode::Saturation;
    if (value == "color") return chronon3d::BlendMode::Color;
    if (value == "luminosity") return chronon3d::BlendMode::Luminosity;
    return std::nullopt;
}

OutputFormat output_format(const std::string& value) {
    if (value == "png_sequence") return OutputFormat::PngSequence;
    if (value == "gif") return OutputFormat::Gif;
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
        const bool has_curve = raw.contains("bezier_curve");
        if ((track.easing == "bezier") != has_curve)
            throw std::runtime_error("camera animation requires easing='bezier' and bezier_curve together");
        if (has_curve) {
            const auto& values = raw.at("bezier_curve");
            if (!values.is_array() || values.size() != 4)
                throw std::runtime_error("camera animation bezier_curve must contain [x1, y1, x2, y2]");
            std::array<float, 4> curve{};
            for (std::size_t i = 0; i < curve.size(); ++i) {
                curve[i] = values.at(i).get<float>();
                if (!std::isfinite(curve[i]))
                    throw std::runtime_error("camera animation bezier_curve values must be finite");
            }
            if (curve[0] < 0.0f || curve[0] > 1.0f || curve[2] < 0.0f || curve[2] > 1.0f ||
                curve[1] < -10.0f || curve[1] > 10.0f || curve[3] < -10.0f || curve[3] > 10.0f)
                throw std::runtime_error("camera animation bezier_curve x handles must be in [0, 1] and y handles in [-10, 10]");
            track.bezier_curve = curve;
        }
        for (const auto& raw_key : raw.at("keyframes")) {
            AnimationKeyframePlan key;
            key.frame = Frame{raw_key.at("frame").get<std::int64_t>()};
            const auto& raw_value = raw_key.at("value");
            if (!raw_value.is_number())
                throw std::runtime_error("camera animation keyframe values must be scalar numbers");
            key.value.push_back(raw_value.get<float>());
            if (raw_key.contains("interpolation")) key.interpolation = raw_key.at("interpolation").get<std::string>();
            if (raw_key.contains("easing")) key.easing = raw_key.at("easing").get<std::string>();
            if (raw_key.contains("bezier_curve")) {
                const auto& raw_curve = raw_key.at("bezier_curve");
                if (!raw_curve.is_array() || raw_curve.size() != 4)
                    throw std::runtime_error("camera keyframe bezier_curve must contain [x1, y1, x2, y2]");
                std::array<float, 4> curve{};
                for (std::size_t i = 0; i < curve.size(); ++i) curve[i] = raw_curve.at(i).get<float>();
                if (!std::all_of(curve.begin(), curve.end(), [](float v) { return std::isfinite(v); }) ||
                    curve[0] < 0.0f || curve[0] > 1.0f || curve[2] < 0.0f || curve[2] > 1.0f ||
                    curve[1] < -10.0f || curve[1] > 10.0f || curve[3] < -10.0f || curve[3] > 10.0f)
                    throw std::runtime_error("camera keyframe bezier_curve handles are out of range");
                key.bezier_curve = curve;
            }
            track.keyframes.push_back(std::move(key));
        }
        tracks.push_back(std::move(track));
    }
    return tracks;
}

}  // namespace chronon3d::render_plan::detail
