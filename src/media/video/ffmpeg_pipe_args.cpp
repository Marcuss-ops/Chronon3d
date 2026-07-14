#include "ffmpeg_pipe_sink.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace chronon3d::media::video {

namespace {

// ============================================================================
//  Map VideoCodec → ffmpeg codec string
// ============================================================================

[[nodiscard]] const char* codec_to_string(VideoCodec codec) noexcept {
    switch (codec) {
        case VideoCodec::H264:  return "libx264";
        case VideoCodec::H265:  return "libx265";
        case VideoCodec::VP9:   return "libvpx-vp9";
        case VideoCodec::AV1:   return "libaom-av1";
        case VideoCodec::Auto:
        default:                return "libx264";
    }
}

[[nodiscard]] const char* container_extension(VideoContainer c) noexcept {
    switch (c) {
        case VideoContainer::Mkv:   return ".mkv";
        case VideoContainer::WebM:  return ".webm";
        case VideoContainer::Raw:   return ".raw";
        case VideoContainer::Mp4:
        default:                    return ".mp4";
    }
}

[[nodiscard]] const char* pix_fmt_to_ffmpeg(PixelFormat fmt) noexcept {
    switch (fmt) {
        case PixelFormat::YUV420P: return "yuv420p";
        case PixelFormat::NV12:    return "nv12";
        case PixelFormat::RGB24:   return "rgb24";
        case PixelFormat::RGBA8:   return "rgba";
    }
    return "yuv420p";
}

} // anonymous namespace

// ============================================================================
//  build_argv() — construct ffmpeg argv vector (executable + arguments)
//
//  Pulled out of ffmpeg_pipe_sink.cpp per P2 item #26 split: pure argument
//  construction (no subprocess launch, no pipe I/O, no state mutation).
// ============================================================================

std::vector<std::string> FfmpegPipeSink::build_argv(const VideoSinkConfig& config) {
    std::vector<std::string> argv;
    argv.reserve(32);

    argv.push_back("ffmpeg");
    // Quiet unless verbose (no verbose flag in VideoSinkConfig, default quiet).
    argv.push_back("-hide_banner");
    argv.push_back("-loglevel");
    argv.push_back("error");
    // Overwrite.
    if (config.output.overwrite) {
        argv.push_back("-y");
    } else {
        argv.push_back("-n");
    }

    // Input: raw video via stdin pipe.
    argv.push_back("-f");
    argv.push_back("rawvideo");
    argv.push_back("-pix_fmt");
    argv.push_back(pix_fmt_to_ffmpeg(config.stream.submitted_format));
    argv.push_back("-s");
    argv.push_back(std::to_string(config.stream.width) + "x" +
                   std::to_string(config.stream.height));
    const auto fps_str = std::to_string(config.stream.frame_rate_num)
                       + "/" + std::to_string(std::max(1, config.stream.frame_rate_den));
    argv.push_back("-r");
    argv.push_back(fps_str);
    argv.push_back("-i");
    argv.push_back("-");

    // No audio.
    argv.push_back("-an");

    // Codec — resolve Auto based on container before converting to string.
    const auto resolved_codec = resolve_auto_codec(
        config.encoder.codec, config.output.container);
    argv.push_back("-c:v");
    argv.push_back(codec_to_string(resolved_codec));

    // CRF / quality.
    if (config.encoder.crf >= 0) {
        argv.push_back("-crf");
        argv.push_back(std::to_string(config.encoder.crf));
    }

    // Preset.
    if (!config.encoder.preset.empty()) {
        argv.push_back("-preset");
        argv.push_back(config.encoder.preset);
    }

    // Tune.
    if (config.encoder.tune.has_value() && !config.encoder.tune->empty()) {
        argv.push_back("-tune");
        argv.push_back(*config.encoder.tune);
    }

    // Bitrate.
    if (config.encoder.bitrate > 0) {
        argv.push_back("-b:v");
        argv.push_back(std::to_string(config.encoder.bitrate));
    }

    // Output (encoded) pixel format.
    argv.push_back("-pix_fmt");
    argv.push_back(pix_fmt_to_ffmpeg(config.encoder.encoded_pixel_format));

    // Color metadata (BT.709 for YUV).
    if (config.encoder.encoded_pixel_format == PixelFormat::YUV420P ||
        config.encoder.encoded_pixel_format == PixelFormat::NV12) {
        argv.push_back("-colorspace");
        argv.push_back("bt709");
        argv.push_back("-color_primaries");
        argv.push_back("bt709");
        argv.push_back("-color_trc");
        argv.push_back("bt709");
        argv.push_back("-color_range");
        argv.push_back("tv");
    }

    // Fragmented MP4 for pipe output (+faststart requires seek, not possible on pipes).
    if (config.output.container == VideoContainer::Mp4) {
        argv.push_back("-movflags");
        argv.push_back("frag_keyframe+empty_moov");
    }

    // Output path.
    std::string output_path = config.output.output_path.string();
    // Append container extension if the filename component has no dot.
    const auto filename = std::filesystem::path(output_path).filename().string();
    if (filename.find('.') == std::string::npos) {
        output_path += container_extension(config.output.container);
    }
    argv.push_back(output_path);

    return argv;
}

} // namespace chronon3d::media::video
