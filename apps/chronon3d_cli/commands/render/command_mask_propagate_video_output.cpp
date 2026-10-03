#include "command_mask_propagate_detail.hpp"
#include "command_mask_propagate_state.hpp"
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
namespace chronon3d::cli::mask_propagation_detail {
std::string output_path(const std::string& pattern, std::size_t frame,
                        const std::string& suffix) {
    const auto marker = pattern.find("####");
    std::string index = std::to_string(frame);
    if (index.size() < 4) index.insert(0, 4 - index.size(), '0');
    if (marker != std::string::npos) {
        auto path = pattern;
        path.replace(marker, 4, index);
        const auto extension = std::filesystem::path(path).extension().string();
        path.insert(path.size() - extension.size(), suffix);
        return path;
    }
    const auto path = std::filesystem::path(pattern);
    return (path.parent_path() /
            (path.stem().string() + "_" + index + suffix + ".png")).string();
}

nlohmann::json encode_propagated_matte_video(const MaskPropagationState& state,
                                            std::uint32_t width,
                                            std::uint32_t height) {
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    if ((width & 1u) != 0 || (height & 1u) != 0)
        throw std::runtime_error("H.264 YUV420 matte output requires even frame dimensions");

    std::ifstream plan_input(state.plan_input, std::ios::binary);
    if (!plan_input) throw std::runtime_error("could not reopen RenderPlan for matte frame rate");
    nlohmann::json plan;
    plan_input >> plan;
    if (!plan.contains("canvas") || !plan["canvas"].is_object())
        throw std::runtime_error("RenderPlan canvas is required to encode matte video");
    const int fps_num = plan["canvas"].value("fps_num", 0);
    const int fps_den = plan["canvas"].value("fps_den", 0);
    if (fps_num <= 0 || fps_den <= 0)
        throw std::runtime_error("RenderPlan has an invalid frame rate for matte encoding");
    const auto& layers = plan.at("layers");
    const auto target = std::find_if(layers.begin(), layers.end(), [&](const auto& layer) {
        return layer.is_object() && layer.value("id", std::string{}) == state.target_layer;
    });
    if (target == layers.end())
        throw std::runtime_error("matte target layer was not found in RenderPlan");
    if (target->contains("time_remap") || target->value("loop", false))
        throw std::runtime_error(
            "native matte encoding requires an unlooped target without time_remap; provide a pre-encoded matte for retimed sources");
    const auto canvas_frames = plan["canvas"].value("duration_frames", std::int64_t{0});
    const auto start_frame = target->value("start_frame", std::int64_t{0});
    const auto target_frames = target->value("duration_frames", canvas_frames - start_frame);
    if (target_frames <= 0 ||
        static_cast<std::uint64_t>(target_frames) != state.frames.size())
        throw std::runtime_error(
            "input frame count must match the target layer duration for synchronized matte encoding");

    const auto container = media::video::classify_video_container(
        std::filesystem::path(state.matte_video).extension().string());
    media::video::VideoSinkConfig config;
    config.stream.width = static_cast<int>(width);
    config.stream.height = static_cast<int>(height);
    config.stream.frame_rate_num = fps_num;
    config.stream.frame_rate_den = fps_den;
    config.stream.submitted_format = media::video::PixelFormat::RGBA8;
    config.encoder.codec = media::video::VideoCodec::H264;
    config.encoder.preset = "ultrafast";
    config.encoder.rate_control_mode = media::video::RateControlMode::Crf;
    config.encoder.crf = 0;
    config.encoder.encoded_pixel_format = media::video::PixelFormat::YUV420P;
    config.output.container = *container;
    config.output.output_path = std::filesystem::absolute(state.matte_video).lexically_normal();
    config.output.overwrite = true;
    if (config.output.output_path.has_parent_path())
        std::filesystem::create_directories(config.output.output_path.parent_path());

    auto sink = media::video::create_video_sink(config);
    if (!sink || !sink->open(config))
        throw std::runtime_error("could not open native matte video encoder: " +
            (sink ? sink->last_error_message() : std::string("native FFmpeg support is unavailable")));

    struct OutputRollback {
        std::filesystem::path path;
        bool active{true};
        ~OutputRollback() {
            if (!active) return;
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } rollback{config.output.output_path};

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t frame_index = 0; frame_index < state.frames.size(); ++frame_index) {
        auto mask = mask_propagation_detail::load_frame(output_path(state.output_pattern, frame_index));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float coverage = std::clamp(mask->get_pixel(
                    static_cast<i32>(x), static_cast<i32>(y)).r, 0.0f, 1.0f);
                const auto code = static_cast<std::uint8_t>(std::clamp(
                    std::lround(color::linear_rec709_float(coverage) * 255.0f), 0l, 255l));
                const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                rgba[offset] = code;
                rgba[offset + 1] = code;
                rgba[offset + 2] = code;
                rgba[offset + 3] = 255;
            }
        }
        const media::video::VideoFrameView frame{
            .data = rgba.data(),
            .stride_bytes = static_cast<std::size_t>(width) * 4,
            .width = static_cast<int>(width),
            .height = static_cast<int>(height),
            .pixel_format = media::video::PixelFormat::RGBA8,
            .pts = static_cast<std::int64_t>(frame_index),
        };
        if (!sink->submit(frame))
            throw std::runtime_error("native matte video frame encoding failed: " +
                                     sink->last_error_message());
    }
    if (!sink->flush())
        throw std::runtime_error("native matte video flush failed: " + sink->last_error_message());
    if (!sink->close())
        throw std::runtime_error("native matte video finalization failed: " + sink->last_error_message());
    std::error_code output_error;
    if (!std::filesystem::is_regular_file(config.output.output_path, output_error) || output_error ||
        std::filesystem::file_size(config.output.output_path, output_error) == 0 || output_error)
        throw std::runtime_error("native matte encoder did not produce a non-empty video file");

    const auto probe = media::video::probe_media(config.output.output_path);
    if (!probe)
        throw std::runtime_error("could not probe encoded matte video: " + probe.error().message);
    const auto video_stream = std::find_if(
        probe->streams.begin(), probe->streams.end(), [](const auto& stream) {
            return stream.kind == media::video::MediaStreamKind::Video;
        });
    const Rational expected_frame_rate = normalize_rational(Rational{fps_num, fps_den});
    const bool frame_rate_matches = video_stream != probe->streams.end() &&
        video_stream->frame_rate.numerator > 0 && video_stream->frame_rate.denominator > 0 &&
        normalize_rational(video_stream->frame_rate) == expected_frame_rate;
    if (video_stream == probe->streams.end() || video_stream->width != static_cast<int>(width) ||
        video_stream->height != static_cast<int>(height) ||
        video_stream->codec != "h264" || video_stream->pixel_format != "yuv420p" ||
        !frame_rate_matches ||
        video_stream->color_range != "tv" || video_stream->color_space != "bt709" ||
        video_stream->color_transfer != "bt709" ||
        video_stream->color_primaries != "bt709")
        throw std::runtime_error(
            "encoded matte video failed its H.264/YUV420P frame-rate, dimensions, or BT.709 color contract");

    media::NativeVideoFrameDecoder decoder;
    decoder.set_software_only(true);
    const auto decoded_count = decoder.indexed_sample_count(
        config.output.output_path.string());
    if (decoded_count != state.frames.size())
        throw std::runtime_error("encoded matte video frame count does not match the input sequence");

    constexpr double kMaximumMattePixelError = 0.02;
    double absolute_error_sum = 0.0;
    double maximum_absolute_error = 0.0;
    std::uint64_t compared_pixels = 0;
    for (std::size_t frame_index = 0; frame_index < state.frames.size(); ++frame_index) {
        const auto path = config.output.output_path.string();
        auto decoded_result = decoder.decode_frame_index_at(
            path, static_cast<std::int64_t>(frame_index),
            static_cast<int>(width), static_cast<int>(height));
        const auto* decoded = media::decoded_frame_if(decoded_result);
        if (!decoded || !decoded->framebuffer)
            throw std::runtime_error("encoded matte video failed software decode at frame " +
                                     std::to_string(frame_index));
        auto expected = mask_propagation_detail::load_frame(output_path(state.output_pattern, frame_index));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float coverage = std::clamp(expected->get_pixel(
                    static_cast<i32>(x), static_cast<i32>(y)).r, 0.0f, 1.0f);
                const Color actual = decoded->framebuffer->get_pixel(
                    static_cast<i32>(x), static_cast<i32>(y));
                const double error = std::max({
                    std::abs(static_cast<double>(actual.r) - coverage),
                    std::abs(static_cast<double>(actual.g) - coverage),
                    std::abs(static_cast<double>(actual.b) - coverage)});
                if (!std::isfinite(error) || error > kMaximumMattePixelError)
                    throw std::runtime_error(
                        "encoded matte pixel error exceeded 0.02 at frame " +
                        std::to_string(frame_index));
                maximum_absolute_error = std::max(maximum_absolute_error, error);
                absolute_error_sum += error;
                ++compared_pixels;
            }
        }
    }
    const auto verification = nlohmann::json{
        {"status", "pass"},
        {"container", probe->format_name},
        {"codec", video_stream->codec},
        {"pixel_format", video_stream->pixel_format},
        {"width", video_stream->width},
        {"height", video_stream->height},
        {"frame_rate_num", fps_num},
        {"frame_rate_den", fps_den},
        {"color_range", video_stream->color_range},
        {"color_space", video_stream->color_space},
        {"color_transfer", video_stream->color_transfer},
        {"color_primaries", video_stream->color_primaries},
        {"decoded_frames", decoded_count},
        {"compared_pixels", compared_pixels},
        {"maximum_absolute_coverage_error", maximum_absolute_error},
        {"mean_absolute_coverage_error", compared_pixels == 0 ? 0.0
            : absolute_error_sum / static_cast<double>(compared_pixels)},
        {"maximum_absolute_coverage_error_allowed", kMaximumMattePixelError},
    };
    rollback.active = false;
    return verification;
#else
    (void)state;
    (void)width;
    (void)height;
    throw std::runtime_error(
        "encoding matte video requires a build with CHRONON3D_ENABLE_NATIVE_FFMPEG");
#endif
}

void append_encoded_video_verification(const MaskPropagationState& state,
                                       const nlohmann::json& verification) {
    const std::filesystem::path report_path = state.output_pattern + ".json";
    std::ifstream input(report_path, std::ios::binary);
    if (!input) throw std::runtime_error("could not reopen mask propagation report");
    nlohmann::json report;
    input >> report;
    if (!report.is_object() || report.value("schema", std::string{}) !=
            "chronon.temporal_mask_propagation.v1")
        throw std::runtime_error("mask propagation report has an unsupported schema");
    report["encoded_matte_video"] = {
        {"path", std::filesystem::absolute(state.matte_video).lexically_normal().string()},
        {"verification", verification},
    };
    const auto temporary = report_path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open temporary matte report");
        output << report.dump(2) << '\n';
        if (!output.good()) throw std::runtime_error("could not write temporary matte report");
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, report_path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish matte video verification report: " +
                                 rename_error.message());
    }
}
} // namespace chronon3d::cli::mask_propagation_detail
