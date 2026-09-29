#include "render_plan_decoder_detail.hpp"
#include "enum_mapping.hpp"

#include <nlohmann/json.hpp>
#include <set>

#include <filesystem>
#include <stdexcept>
#include <set>
#include <array>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace chronon3d::render_plan::detail {
using namespace std::literals;

ShapePathCommandPlan decode_shape_path_command(
    const nlohmann::json& value, std::string_view context) {
    const auto fail = [context](std::string_view reason) {
        throw std::runtime_error(std::string(context) + " " + std::string(reason));
    };
    if (!value.is_object() || !value.contains("type"))
        fail("command requires a type");
    ShapePathCommandPlan command;
    const auto type = value.at("type").get<std::string>();
    if (type == "move_to") command.type = ShapePathCommandTypePlan::MoveTo;
    else if (type == "line_to") command.type = ShapePathCommandTypePlan::LineTo;
    else if (type == "quadratic_to") command.type = ShapePathCommandTypePlan::QuadraticTo;
    else if (type == "cubic_to") command.type = ShapePathCommandTypePlan::CubicTo;
    else if (type == "close") command.type = ShapePathCommandTypePlan::Close;
    else fail("has unsupported type: " + type);

    const bool has_point = value.contains("point");
    const bool has_c1 = value.contains("control1");
    const bool has_c2 = value.contains("control2");
    const auto read_point = [&](const char* key, std::array<float, 2>& output) {
        const auto& point = value.at(key);
        if (!point.is_array() || point.size() != 2)
            fail("coordinates must contain exactly two numbers");
        output = {point[0].get<float>(), point[1].get<float>()};
        if (!std::isfinite(output[0]) || !std::isfinite(output[1]))
            fail("coordinates must be finite");
    };
    if (has_point) read_point("point", command.point);
    if (has_c1) read_point("control1", command.control1);
    if (has_c2) read_point("control2", command.control2);

    if ((command.type == ShapePathCommandTypePlan::MoveTo ||
         command.type == ShapePathCommandTypePlan::LineTo) && (!has_point || has_c1 || has_c2))
        fail("move_to/line_to require point only");
    if (command.type == ShapePathCommandTypePlan::QuadraticTo && (!has_point || !has_c1 || has_c2))
        fail("quadratic_to requires point and control1 only");
    if (command.type == ShapePathCommandTypePlan::CubicTo && (!has_point || !has_c1 || !has_c2))
        fail("cubic_to requires point and both controls");
    if (command.type == ShapePathCommandTypePlan::Close && (has_point || has_c1 || has_c2))
        fail("close accepts no coordinates");
    return command;
}

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
    static constexpr std::array kValues{
        std::pair{"image"sv, LayerType::Image}, std::pair{"video"sv, LayerType::Video},
        std::pair{"text"sv, LayerType::Text}, std::pair{"color"sv, LayerType::Color},
        std::pair{"shape"sv, LayerType::Shape}, std::pair{"particles"sv, LayerType::Particles},
        std::pair{"light"sv, LayerType::Light}, std::pair{"adjustment"sv, LayerType::Adjustment},
        std::pair{"mesh"sv, LayerType::Mesh},
    };
    return enum_from_string(value, kValues, "unsupported primitive layer type: ");
}

FitMode fit_mode(const std::string& value) {
    static constexpr std::array kValues{
        std::pair{"contain"sv, FitMode::Contain},
        std::pair{"stretch"sv, FitMode::Stretch},
        std::pair{"none"sv, FitMode::None},
        std::pair{"cover"sv, FitMode::Cover},
    };
    return find_enum_value(value, kValues).value_or(FitMode::Cover);
}

std::optional<chronon3d::BlendMode> blend_mode(const std::string& value) {
    using Mode = chronon3d::BlendMode;
    static constexpr std::array kValues{
        std::pair{"normal"sv, Mode::Normal}, std::pair{"add"sv, Mode::Add},
        std::pair{"multiply"sv, Mode::Multiply}, std::pair{"screen"sv, Mode::Screen},
        std::pair{"overlay"sv, Mode::Overlay}, std::pair{"darken"sv, Mode::Darken},
        std::pair{"lighten"sv, Mode::Lighten}, std::pair{"difference"sv, Mode::Difference},
        std::pair{"exclusion"sv, Mode::Exclusion}, std::pair{"soft_light"sv, Mode::SoftLight},
        std::pair{"hard_light"sv, Mode::HardLight}, std::pair{"color_dodge"sv, Mode::ColorDodge},
        std::pair{"color_burn"sv, Mode::ColorBurn}, std::pair{"subtract"sv, Mode::Subtract},
        std::pair{"divide"sv, Mode::Divide}, std::pair{"vivid_light"sv, Mode::VividLight},
        std::pair{"pin_light"sv, Mode::PinLight}, std::pair{"linear_light"sv, Mode::LinearLight},
        std::pair{"hue"sv, Mode::Hue}, std::pair{"saturation"sv, Mode::Saturation},
        std::pair{"color"sv, Mode::Color}, std::pair{"luminosity"sv, Mode::Luminosity},
    };
    return find_enum_value(value, kValues);
}

OutputFormat output_format(const std::string& value) {
    static constexpr std::array kValues{
        std::pair{"png_sequence"sv, OutputFormat::PngSequence},
        std::pair{"gif"sv, OutputFormat::Gif}, std::pair{"mp4"sv, OutputFormat::Mp4},
        std::pair{"mkv"sv, OutputFormat::Mkv}, std::pair{"webm"sv, OutputFormat::WebM},
    };
    return find_enum_value(value, kValues).value_or(OutputFormat::Png);
}

VideoCodec video_codec(const std::string& value) {
    static constexpr std::array kValues{
        std::pair{"h264"sv, VideoCodec::H264}, std::pair{"h265"sv, VideoCodec::H265},
        std::pair{"vp9"sv, VideoCodec::VP9}, std::pair{"av1"sv, VideoCodec::AV1},
    };
    return find_enum_value(value, kValues).value_or(VideoCodec::Auto);
}

CameraPropertyPlan camera_property(const std::string& value) {
    static constexpr std::array kValues{
        std::pair{"camera_position_x"sv, CameraPropertyPlan::PositionX},
        std::pair{"camera_position_y"sv, CameraPropertyPlan::PositionY},
        std::pair{"camera_position_z"sv, CameraPropertyPlan::PositionZ},
        std::pair{"camera_rotation_x"sv, CameraPropertyPlan::RotationX},
        std::pair{"camera_rotation_y"sv, CameraPropertyPlan::RotationY},
        std::pair{"camera_rotation_z"sv, CameraPropertyPlan::RotationZ},
        std::pair{"camera_fov_deg"sv, CameraPropertyPlan::FovDeg},
        std::pair{"camera_zoom"sv, CameraPropertyPlan::Zoom},
    };
    return enum_from_string(value, kValues, "unsupported camera animation property: ");
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
    if (value.contains("dof")) {
        const auto& raw_dof = value.at("dof");
        if (!raw_dof.is_object())
            throw std::runtime_error("camera.dof must be an object");
        camera.dof_enabled = raw_dof.value("enabled", false);
        camera.dof_focus_distance = raw_dof.value("focus_distance", camera.dof_focus_distance);
        camera.dof_aperture = raw_dof.value("aperture", camera.dof_aperture);
        camera.dof_max_blur = raw_dof.value("max_blur", camera.dof_max_blur);
        if (!std::isfinite(camera.dof_focus_distance) || camera.dof_focus_distance <= 0.0f ||
            camera.dof_focus_distance > 10000000.0f ||
            !std::isfinite(camera.dof_aperture) || camera.dof_aperture < 0.0f ||
            camera.dof_aperture > 1.0f || !std::isfinite(camera.dof_max_blur) ||
            camera.dof_max_blur < 0.0f || camera.dof_max_blur > 256.0f)
            throw std::runtime_error("camera.dof values are outside supported bounds");
    }
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
        std::set<std::string> track_frames;
        for (const auto& raw_key : raw.at("keyframes"))
            if (!raw_key.is_object() || !raw_key.contains("frame") ||
                !track_frames.insert(raw_key.at("frame").dump()).second)
                throw std::runtime_error("camera keyframe frames must be unique");
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
        std::set<std::int64_t> frames;
        for (const auto& raw_key : raw.at("keyframes")) {
            AnimationKeyframePlan key;
            key.frame = Frame{raw_key.at("frame").get<std::int64_t>()};
            if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                !frames.insert(key.frame.integral()).second)
                throw std::runtime_error("camera keyframe frames must be unique and in [0, 1000000]");
            const auto& raw_value = raw_key.at("value");
            if (!raw_value.is_number())
                throw std::runtime_error("camera animation keyframe values must be scalar numbers");
            const float scalar = raw_value.get<float>();
            if (!std::isfinite(scalar))
                throw std::runtime_error("camera keyframe value must be finite");
            key.value.push_back(scalar);
            if (raw_key.contains("interpolation")) key.interpolation = raw_key.at("interpolation").get<std::string>();
            if (raw_key.contains("easing")) key.easing = raw_key.at("easing").get<std::string>();
            key.roving = raw_key.value("roving", false);
            if (raw_key.contains("roving") && !raw_key.at("roving").is_boolean())
                throw std::runtime_error("camera keyframe roving must be a boolean");
            if (raw_key.contains("temporal_in_dx")) key.temporal_in_dx = raw_key.at("temporal_in_dx").get<float>();
            if (raw_key.contains("temporal_in_dy")) key.temporal_in_dy = raw_key.at("temporal_in_dy").get<float>();
            if (raw_key.contains("temporal_out_dx")) key.temporal_out_dx = raw_key.at("temporal_out_dx").get<float>();
            if (raw_key.contains("temporal_out_dy")) key.temporal_out_dy = raw_key.at("temporal_out_dy").get<float>();
            for (const auto handle : {key.temporal_in_dx, key.temporal_in_dy,
                                      key.temporal_out_dx, key.temporal_out_dy})
                if (handle && !std::isfinite(*handle))
                    throw std::runtime_error("camera temporal handles must be finite");
            if (key.temporal_in_dx.has_value() != key.temporal_in_dy.has_value() ||
                key.temporal_out_dx.has_value() != key.temporal_out_dy.has_value())
                throw std::runtime_error("camera temporal dx and dy must be provided together");
            if (key.temporal_in_dx && *key.temporal_in_dx > 0.0f)
                throw std::runtime_error("camera temporal_in_dx must be non-positive");
            if (key.temporal_out_dx && *key.temporal_out_dx < 0.0f)
                throw std::runtime_error("camera temporal_out_dx must be non-negative");
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
            if (key.interpolation == "bezier" && key.bezier_curve &&
                (key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy))
                throw std::runtime_error("camera bezier_curve and temporal handles cannot be combined");
            if (key.interpolation && *key.interpolation != "linear" &&
                *key.interpolation != "hold" && *key.interpolation != "bezier" &&
                *key.interpolation != "auto_bezier")
                throw std::runtime_error("camera interpolation must be linear, hold, bezier, or auto_bezier");
            const bool has_temporal_handles = key.temporal_in_dx || key.temporal_in_dy ||
                key.temporal_out_dx || key.temporal_out_dy;
            if (key.roving && (has_temporal_handles || key.bezier_curve))
                throw std::runtime_error("camera roving cannot contain authored Bezier handles");
            if ((key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy) &&
                (key.interpolation != "bezier" || key.bezier_curve))
                throw std::runtime_error("camera temporal handles require interpolation='bezier' without bezier_curve");
            if (key.roving && key.interpolation == "auto_bezier")
                throw std::runtime_error("camera roving and auto_bezier cannot be combined");
            if (key.interpolation == "bezier" && !key.bezier_curve &&
                !(key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy))
                throw std::runtime_error("camera bezier interpolation requires a curve or temporal handles");
            if (key.interpolation && *key.interpolation != "bezier" && key.bezier_curve)
                throw std::runtime_error("camera bezier_curve requires interpolation='bezier'");
            if (key.interpolation == "auto_bezier" &&
                (key.temporal_in_dx || key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
                throw std::runtime_error("camera auto_bezier cannot combine with authored handles");
            if (key.interpolation && key.easing)
                throw std::runtime_error("camera keyframe interpolation cannot be combined with easing");
            if (!key.interpolation && ((key.easing == "bezier") != key.bezier_curve.has_value()))
                throw std::runtime_error("camera per-key easing='bezier' requires bezier_curve");
            if (key.interpolation == "auto_bezier" && (key.easing || key.bezier_curve))
                throw std::runtime_error("camera auto_bezier cannot combine with per-key easing or bezier_curve");
            track.keyframes.push_back(std::move(key));
        }
        tracks.push_back(std::move(track));
    }
    return tracks;
}

}  // namespace chronon3d::render_plan::detail
