#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/camera_contract.hpp>
#include "json_vector.hpp"
#include "enum_mapping.hpp"

#include <nlohmann/json.hpp>
#include <set>

#include <filesystem>
#include <stdexcept>
#include <set>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
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
        const auto raw = decode_json_vector<2>(object.at(key),
            std::string("gradient.") + key + " must have two numbers");
        output = {raw[0], raw[1]};
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
        const auto hdr_component = [](float component) {
            return std::isfinite(component) && component >= 0.0f && component <= 4.0f;
        };
        const auto alpha_component = [](float component) {
            return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
        };
        if (!alpha_component(position) || !hdr_component(decoded_color.r) ||
            !hdr_component(decoded_color.g) || !hdr_component(decoded_color.b) ||
            !alpha_component(decoded_color.a))
            throw std::runtime_error("gradient stops require finite RGB in [0, 4], alpha and position in [0, 1]");
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

CameraPlan decode_camera(const nlohmann::json& value) {
    CameraPlan camera;
    camera.type = value.value("type", std::string{"perspective"});
    if (camera.type != "perspective" && camera.type != "orthographic")
        throw std::runtime_error("camera.type must be perspective or orthographic: " + camera.type);

    const auto read_vec3 = [](const nlohmann::json& object, const char* key,
                              std::array<float, 3>& result) {
        if (!object.contains(key)) return;
        result = decode_json_vector<3>(object.at(key),
            std::string("camera.") + key + " must contain exactly 3 numbers");
    };
    read_vec3(value, "position", camera.position);
    read_vec3(value, "rotation_deg", camera.rotation_deg);
    camera.fov_deg = value.value("fov_deg", 50.0f);
    camera.near = value.value("near", 1.0f);
    camera.far = value.value("far", 10000.0f);
    camera.zoom = value.value("zoom", 1.0f);
    // Physical lens authoring (0 = not authored). Unknown camera-level fields
    // (e.g. node/target hints) stay ignored: plans in the wild carry them.
    camera.focal_length_mm = value.value("focal_length_mm", camera.focal_length_mm);
    camera.sensor_width_mm = value.value("sensor_width_mm", camera.sensor_width_mm);
    if (value.contains("lens_shift"))
        camera.lens_shift = decode_json_vector<2>(value.at("lens_shift"), "camera.lens_shift must contain exactly 2 numbers");
    camera.f_stop = value.value("f_stop", camera.f_stop);
    camera.iris_blades = value.value("iris_blades", camera.iris_blades);
    // Range admission for every decoded camera value is the single
    // camera_contract_problem() call below. Decoder-local range checks are
    // demolished so the admission contract has exactly one seat.
    if (value.contains("dof")) {
        const auto& raw_dof = value.at("dof");
        if (!raw_dof.is_object())
            throw std::runtime_error("camera.dof must be an object");
        // The nested lens fields remain accepted for wire compatibility, but
        // are normalized into the single camera-level authorities. Conflicts
        // fail closed instead of creating a second typed value.
        if (raw_dof.contains("aperture"))
            throw std::runtime_error(
                "camera.dof.aperture was retired: author camera.f_stop instead "
                "(legacy gain = 0.672 / f_stop, CM3D blurGainFromFStop)");
        camera.dof_enabled = raw_dof.value("enabled", false);
        camera.dof_focus_distance = raw_dof.value("focus_distance", camera.dof_focus_distance);
        camera.dof_max_blur = raw_dof.value("max_blur", camera.dof_max_blur);
        if (raw_dof.contains("f_stop")) {
            const float nested_f_stop = raw_dof.at("f_stop").get<float>();
            if (nested_f_stop > 0.f && camera.f_stop > 0.f && nested_f_stop != camera.f_stop)
                throw std::runtime_error(
                    "camera.f_stop and camera.dof.f_stop must agree when both are authored");
            if (camera.f_stop == 0.f) camera.f_stop = nested_f_stop;
        }
        if (raw_dof.contains("iris_blades")) {
            const int nested_iris_blades = raw_dof.at("iris_blades").get<int>();
            if (nested_iris_blades != 0 && camera.iris_blades != 0 &&
                nested_iris_blades != camera.iris_blades)
                throw std::runtime_error(
                    "camera.iris_blades and camera.dof.iris_blades must agree when both are authored");
            if (camera.iris_blades == 0) camera.iris_blades = nested_iris_blades;
        }
        camera.dof_highlight_gain = raw_dof.value("highlight_gain", camera.dof_highlight_gain);
    }
    if (const auto problem = camera_contract_problem(camera))
        throw std::runtime_error(*problem);
    return camera;
}

std::vector<CameraSamplePlan> decode_camera_samples(const nlohmann::json& value) {
    if (!value.is_array() || value.empty() || value.size() > 1000000)
        throw std::runtime_error("camera_samples must be a non-empty array of at most 1000000 samples");
    std::vector<CameraSamplePlan> samples;
    samples.reserve(value.size());
    double previous = -std::numeric_limits<double>::infinity();
    for (const auto& raw : value) {
        if (!raw.is_object() || !raw.contains("frame") || !raw.contains("camera") ||
            !raw.at("frame").is_number())
            throw std::runtime_error("camera sample requires numeric frame and camera object");
        const double frame = raw.at("frame").get<double>();
        if (!std::isfinite(frame) || frame <= previous || frame < -1000000.0 || frame > 1000000.0)
            throw std::runtime_error("camera sample frames must be finite, strictly increasing, and in [-1000000, 1000000]");
        samples.push_back(CameraSamplePlan{frame, decode_camera(raw.at("camera"))});
        previous = frame;
    }
    return samples;
}

}  // namespace chronon3d::render_plan::detail
