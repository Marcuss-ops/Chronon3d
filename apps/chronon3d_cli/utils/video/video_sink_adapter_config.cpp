#include "video_sink_adapter.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

chronon3d::media::video::VideoCodec
VideoSinkEncoderAdapter::map_codec(const std::string& codec) {
    using media::video::VideoCodec;
    if (codec == "h264_nvenc") {
        spdlog::warn("[video_adapter] Hardware codec '{}' is unavailable in CPU-only mode; falling back to libx264", codec);
        return VideoCodec::H264;
    }
    if (codec == "h264_amf" || codec == "h264_qsv" || codec == "h264_videotoolbox") {
        spdlog::warn("[video_adapter] Hardware codec '{}' is unavailable in CPU-only mode; falling back to libx264", codec);
        return VideoCodec::H264;
    }
    if (codec == "hevc_nvenc" || codec == "hevc_amf" ||
        codec == "hevc_qsv" || codec == "hevc_videotoolbox") {
        spdlog::warn("[video_adapter] Hardware codec '{}' is unavailable in CPU-only mode; falling back to libx265", codec);
        return VideoCodec::H265;
    }
    if (codec == "libx264" || codec == "h264") return VideoCodec::H264;
    if (codec == "libx265" || codec == "h265" || codec == "hevc") return VideoCodec::H265;
    if (codec == "libvpx-vp9" || codec == "vp9") return VideoCodec::VP9;
    if (codec == "libaom-av1" || codec == "av1") return VideoCodec::AV1;
    if (codec == "uncompressed" || codec == "raw") return VideoCodec::Uncompressed;
    return VideoCodec::Auto;
}

chronon3d::media::video::PixelFormat
VideoSinkEncoderAdapter::map_pixel_format(PipePixelFormat fmt) {
    using media::video::PixelFormat;
    switch (fmt) {
        case PipePixelFormat::RGBA: return PixelFormat::RGBA8;
        case PipePixelFormat::YUV420P: return PixelFormat::YUV420P;
        case PipePixelFormat::NV12: return PixelFormat::NV12;
    }
    return PixelFormat::RGBA8;
}

chronon3d::media::video::PixelFormat
VideoSinkEncoderAdapter::map_output_pix_fmt(const std::string& fmt) {
    using media::video::PixelFormat;
    if (fmt == "yuv420p") return PixelFormat::YUV420P;
    if (fmt == "nv12") return PixelFormat::NV12;
    if (fmt == "rgb24") return PixelFormat::RGB24;
    if (fmt == "rgba") return PixelFormat::RGBA8;
    return PixelFormat::YUV420P;
}

chronon3d::media::video::VideoContainer
VideoSinkEncoderAdapter::detect_container(const std::string& path) {
    using media::video::VideoContainer;
    const auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".mp4" || ext == ".MP4") return VideoContainer::Mp4;
    if (ext == ".mkv" || ext == ".MKV") return VideoContainer::Mkv;
    if (ext == ".webm" || ext == ".WEBM") return VideoContainer::WebM;
    if (ext == ".raw" || ext == ".yuv") return VideoContainer::Raw;
    return VideoContainer::Mp4;
}

bool VideoSinkEncoderAdapter::build_sink_config(
    const FfmpegPipeOptions& opts,
    chronon3d::media::video::VideoSinkConfig& config) {
    using media::video::VideoCodec;
    using media::video::VideoContainer;
    using media::video::RateControlMode;

    config.stream.width = opts.width;
    config.stream.height = opts.height;
    config.stream.frame_rate_num = opts.canonical_fps_num();
    config.stream.frame_rate_den = opts.canonical_fps_den();
    config.stream.submitted_format = map_pixel_format(opts.input_format);

    if (sink_type_ == VideoSinkType::RawFile) {
        config.encoder.codec = VideoCodec::Uncompressed;
        config.encoder.rate_control_mode = RateControlMode::Bitrate;
        config.encoder.crf = -1;
        config.encoder.qp = -1;
        config.encoder.bitrate = 0;
    } else {
        const auto container = detect_container(opts.output_path);
        auto raw_codec = map_codec(opts.codec);
        config.encoder.codec = media::video::resolve_auto_codec(raw_codec, container);
        if (opts.rate_control_mode == "qp") {
            config.encoder.rate_control_mode = RateControlMode::ConstantQp;
            config.encoder.qp = opts.qp;
            config.encoder.crf = -1;
            config.encoder.bitrate = 0;
        } else if (opts.rate_control_mode == "bitrate") {
            config.encoder.rate_control_mode = RateControlMode::Bitrate;
            config.encoder.bitrate = opts.bitrate;
            config.encoder.crf = -1;
            config.encoder.qp = -1;
        } else {
            config.encoder.rate_control_mode = RateControlMode::Crf;
            config.encoder.crf = opts.crf;
            config.encoder.qp = -1;
            config.encoder.bitrate = 0;
        }
        config.encoder.preset = opts.preset;
        config.encoder.tune = opts.tune.empty() ? std::nullopt : std::make_optional(opts.tune);
    }

    config.encoder.encoded_pixel_format = map_output_pix_fmt(opts.output_pix_fmt);
    config.encoder.apply_gamma = opts.color_transform.apply_gamma;
    config.transport.asynchronous = false;
    config.transport.queue_capacity = 4;
    if (opts.pipe_writer == "io_uring") {
        spdlog::warn("[video_adapter] io_uring pipe writer is not implemented; falling back to classic synchronous pipe");
    }
    config.transport.use_io_uring = false;
    config.transport.write_timeout = std::chrono::milliseconds(30000);
    config.output.container = sink_type_ == VideoSinkType::RawFile
        ? VideoContainer::Raw : detect_container(opts.output_path);
    config.output.output_path = std::filesystem::path(opts.output_path);
    config.output.overwrite = true;
    config.label = sink_type_ == VideoSinkType::RawFile
        ? "adapter-raw-video-sink" : "adapter-ffmpeg-pipe-sink";
    return true;
}

} // namespace chronon3d::cli
