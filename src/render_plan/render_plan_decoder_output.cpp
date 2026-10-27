#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

std::optional<PlanDecodeError> decode_output_settings(
    const nlohmann::json& raw_output, RenderPlan& plan) {
    const auto& output = raw_output;
    plan.output.path = output.at("path").get<std::string>();
    plan.output.color_space = output.value("color_space", std::string{"srgb"});
    plan.output.reference_white_nits = output.value(
        "reference_white_nits", 100.0f);
    if (plan.output.color_space != "srgb" &&
        plan.output.color_space != "rec709" &&
        plan.output.color_space != "linearsrgb" &&
        plan.output.color_space != "pq" &&
        plan.output.color_space != "hlg")
        return PlanDecodeError{"output.color_space",
            "must be srgb, rec709, linearsrgb, pq, or hlg",
            "output_color_space_invalid", "render-plan-color"};
    if (!std::isfinite(plan.output.reference_white_nits) ||
        plan.output.reference_white_nits <= 0.0f ||
        plan.output.reference_white_nits > 10000.0f)
        return PlanDecodeError{"output.reference_white_nits",
            "must be finite, greater than 0, and at most 10000 nits",
            "reference_white_invalid", "render-plan-color"};
    if (output.contains("format"))
        plan.output.format = output_format(output.at("format").get<std::string>());
    if (output.contains("codec"))
        plan.output.codec = video_codec(output.at("codec").get<std::string>());
    if (output.contains("hdr_mastering_metadata")) {
        const auto& raw = output.at("hdr_mastering_metadata");
        chronon3d::color::HdrMasteringMetadata metadata;
        const auto read_xy = [&](const nlohmann::json& point)
            -> std::optional<std::array<float, 2>> {
            if (!point.is_array() || point.size() != 2)
                return std::nullopt;
            const float x = point.at(0).get<float>();
            const float y = point.at(1).get<float>();
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f ||
                y < 0.0f || x > 1.0f || y > 1.0f || x + y > 1.0f)
                return std::nullopt;
            return std::array<float, 2>{x, y};
        };
        const auto& primaries = raw.at("display_primaries");
        bool valid_metadata = primaries.is_array() && primaries.size() == 3;
        if (valid_metadata) {
            for (std::size_t index = 0; index < 3; ++index) {
                const auto point = read_xy(primaries.at(index));
                if (!point) { valid_metadata = false; break; }
                metadata.display_primaries[index] = *point;
            }
        }
        const auto white_point = read_xy(raw.at("white_point"));
        if (white_point) metadata.white_point = *white_point;
        else valid_metadata = false;
        metadata.min_luminance_nits = raw.at("min_luminance_nits").get<float>();
        metadata.max_luminance_nits = raw.at("max_luminance_nits").get<float>();
        valid_metadata = valid_metadata &&
            std::isfinite(metadata.min_luminance_nits) &&
            std::isfinite(metadata.max_luminance_nits) &&
            metadata.min_luminance_nits >= 0.0f &&
            metadata.max_luminance_nits > metadata.min_luminance_nits &&
            metadata.max_luminance_nits <= 10000.0f;
        if (raw.contains("content_light")) {
            const auto& light = raw.at("content_light");
            chronon3d::color::ContentLightMetadata content{
                light.at("max_cll_nits").get<std::uint32_t>(),
                light.at("max_fall_nits").get<std::uint32_t>()};
            if (content.max_fall_nits > content.max_cll_nits ||
                content.max_cll_nits > 10000 || content.max_fall_nits > 10000)
                valid_metadata = false;
            metadata.content_light = content;
        }
        if (!valid_metadata)
            return PlanDecodeError{"output.hdr_mastering_metadata",
                "mastering xy coordinates must be finite valid chromaticities; luminance must satisfy 0 <= min < max <= 10000 nits and MaxFALL <= MaxCLL",
                "hdr_mastering_metadata_invalid", "render-plan-color"};
        if ((plan.output.color_space != "pq" && plan.output.color_space != "hlg") ||
            (plan.output.format != OutputFormat::Mp4 &&
             plan.output.format != OutputFormat::Mkv &&
             plan.output.format != OutputFormat::WebM))
            return PlanDecodeError{"output.hdr_mastering_metadata",
                "HDR mastering metadata requires PQ/HLG video output",
                "hdr_mastering_requires_hdr_video", "render-plan-color"};
        plan.output.hdr_mastering_metadata = std::move(metadata);
    }
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
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
