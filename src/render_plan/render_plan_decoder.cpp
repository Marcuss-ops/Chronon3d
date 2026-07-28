#include <chronon3d/render_plan/render_plan.hpp>

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace chronon3d::render_plan {
namespace {

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

LayerType layer_type(const std::string& value) {
    if (value == "image") return LayerType::Image;
    if (value == "video") return LayerType::Video;
    if (value == "text") return LayerType::Text;
    if (value == "subtitle_track") return LayerType::SubtitleTrack;
    return LayerType::Color;
}

FitMode fit_mode(const std::string& value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
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

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});
    layer.font = value.value("font", std::string{});
    layer.preset = value.value("preset", std::string{});
    layer.font_size = optional_value<float>(value, "font_size");
    layer.box_width = optional_value<float>(value, "box_width");
    layer.box_height = optional_value<float>(value, "box_height");
    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t index = 0; index < position.size(); ++index)
            layer.position[index] = position.at(index).get<float>();
    }
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    if (value.contains("format"))
        layer.subtitle_format = output_format(value.at("format").get<std::string>());
    if (value.contains("animation")) {
        const auto& animation = value.at("animation");
        layer.animation = AnimationTiming{
            optional_frame(animation, "start_frame"),
            optional_frame(animation, "duration_frames"),
            animation.at("preset").get<std::string>()};
    }
    return layer;
}

}  // namespace

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    const auto validation = validate_render_plan(root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        RenderPlan plan;
        plan.job_id = root.value("job_id", std::string{"chronon_plan"});
        plan.assets_root = root.value("assets_root", std::string{});
        const auto& canvas = root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            canvas.at("fps").get<int>(),
            Frame{canvas.at("duration_frames").get<std::int64_t>()}};
        for (const auto& layer : root.at("layers")) plan.layers.push_back(decode_layer(layer));
        for (const auto& value : root.value("audio_tracks", nlohmann::json::array())) {
            plan.audio_tracks.push_back(AudioTrackPlan{
                value.at("source").get<std::string>(),
                value.value("volume", 1.0),
                value.value("start_time_offset", 0.0),
                value.value("duration_seconds", 0.0),
                value.value("role", std::string{})});
        }
        const auto& output = root.at("output");
        plan.output.path = output.at("path").get<std::string>();
        if (output.contains("format"))
            plan.output.format = output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            plan.output.codec = video_codec(output.at("codec").get<std::string>());
        plan.output.bitrate = output.value("bitrate", std::int64_t{0});
        plan.output.crf = output.value("crf", 0);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
