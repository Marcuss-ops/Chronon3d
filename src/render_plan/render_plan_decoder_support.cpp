#include "render_plan_decoder_detail.hpp"

#include <filesystem>
#include <stdexcept>

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

}  // namespace chronon3d::render_plan::detail
