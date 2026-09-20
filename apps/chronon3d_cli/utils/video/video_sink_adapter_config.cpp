#include "video_sink_adapter.hpp"
#include "encoder_config_resolution.hpp"

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
        case PipePixelFormat::P010: return PixelFormat::P010;
    }
    return PixelFormat::RGBA8;
}

std::optional<chronon3d::media::video::PixelFormat>
VideoSinkEncoderAdapter::map_output_pix_fmt(const std::string& fmt) {
    // ONE parser, shared with the pipe vocabulary via
    // <chronon3d/runtime/frame_format.hpp>. The mapping used to be a private
    // table here that knew only four names (so a truthful `p010` encoder output
    // was silently encoded as 8-bit yuv420p) and returned yuv420p for every
    // unknown name instead of failing.
    //
    // No translation is needed: media::video::PixelFormat IS
    // runtime::PixelFormat (see media/video/video_frame.hpp).
    return runtime::try_parse_encoder_output_pixel_format(fmt);
}

chronon3d::media::video::VideoContainer
VideoSinkEncoderAdapter::detect_container(const std::string& path) {
    using chronon3d::media::video::VideoContainer;
    const auto ext = std::filesystem::path(path).extension().string();
    // AUDIT P1-3 — one vocabulary. The local table here knew only
    // `.mp4`/`.MP4`, `.mkv`/`.MKV`, `.webm`/`.WEBM`: the case handling was
    // hand-rolled and inconsistent with the job validator and the asset ingest
    // tables, so `.Mp4` classified differently depending on which layer asked.
    if (const auto container = media::video::classify_video_container(ext)) {
        return *container;
    }
    if (ext == ".raw" || ext == ".yuv") return VideoContainer::Raw;
    // Tracked silent default (audit §6): an unrecognized extension is treated as
    // mp4 rather than reported. Left as-is because the call site has no
    // failure channel, but named here so it is a decision and not an accident.
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

        // ── Single-authority encoder configuration ─────────────────────
        // This adapter runs the CPU frame-submit encoder path. For the
        // libx264 family the encoder-configuration resolver is the only place
        // that interprets rate-control intent, presets and tunes: unknown
        // rate-control strings, bitrate/QP modes without their required
        // value, invalid presets and unsupported tunes fail fast here instead
        // of silently degrading to CRF or the driver default. NVENC never
        // reaches this adapter in native builds (NativeAvEncoder owns it);
        // when native FFmpeg is disabled, the media sink factory fails closed;
        // unknown rate-control strings still fail below.
        const bool x264_family =
            opts.codec == "libx264" || opts.codec == "libx264rgb" ||
            opts.codec == "h264" || opts.codec == "auto";
        const bool nvenc_requested =
            resolve_encoder_backend(opts.codec, opts.hardware_encoder) ==
            EncoderBackend::Nvenc;
        const bool resolve_quality =
            x264_family && !nvenc_requested;

        if (resolve_quality) {
            const auto resolution =
                resolve_encoder_config(make_encoder_config_request(opts));
            if (!resolution) {
                spdlog::error("[video_adapter] Invalid encoder configuration: {}",
                              resolution.error().message);
                return false;
            }
            const auto& resolved = resolution.value();
            switch (resolved.rate_control()) {
                case ResolvedEncoderRateControl::ConstantQuality:
                    config.encoder.rate_control_mode = RateControlMode::Crf;
                    config.encoder.crf = resolved.crf().value_or(-1);
                    config.encoder.qp = -1;
                    config.encoder.bitrate = 0;
                    break;
                case ResolvedEncoderRateControl::ConstantQp:
                    config.encoder.rate_control_mode = RateControlMode::ConstantQp;
                    config.encoder.qp = resolved.qp().value_or(-1);
                    config.encoder.crf = -1;
                    config.encoder.bitrate = 0;
                    break;
                case ResolvedEncoderRateControl::Vbr:
                case ResolvedEncoderRateControl::Cbr:
                    config.encoder.rate_control_mode = RateControlMode::Bitrate;
                    config.encoder.bitrate = resolved.bitrate().value_or(0);
                    config.encoder.crf = -1;
                    config.encoder.qp = -1;
                    break;
                case ResolvedEncoderRateControl::DriverDefault:
                    // Only NVENC produces this; guarded above by
                    // resolve_quality, but keep the config explicit.
                    config.encoder.rate_control_mode = RateControlMode::Crf;
                    config.encoder.crf = -1;
                    config.encoder.qp = -1;
                    config.encoder.bitrate = 0;
                    break;
            }
            config.encoder.preset = resolved.preset();
            config.encoder.tune = resolved.tune();
        } else {
            // Non-libx264 codec families (h265/vp9/av1/...) or the degraded
            // NVENC-on-pipe fallback: the media layer owns their semantics.
            // Unknown rate-control strings must still never silently become
            // CRF (the historical `else → Crf` trap).
            if (!opts.rate_control_mode.empty()) {
                EncoderRateControlRequest parsed;
                if (!parse_rate_control_request(opts.rate_control_mode, parsed)) {
                    spdlog::error(
                        "[video_adapter] Unknown rate-control mode '{}'; "
                        "allowed modes: crf, qp, bitrate",
                        opts.rate_control_mode);
                    return false;
                }
            }
            if (opts.rate_control_mode == "qp") {
                config.encoder.rate_control_mode = RateControlMode::ConstantQp;
                config.encoder.qp = opts.qp;
                config.encoder.crf = -1;
                config.encoder.bitrate = 0;
            } else if (opts.rate_control_mode == "bitrate" ||
                       opts.rate_control_mode == "vbr" ||
                       opts.rate_control_mode == "cbr") {
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
    }

    const auto encoded_pixel_format = map_output_pix_fmt(opts.output_pix_fmt);
    if (!encoded_pixel_format) {
        // Fail closed. Substituting yuv420p here would produce a formally
        // successful export in the wrong pixel format, which is exactly the
        // failure mode the canonical parser exists to prevent.
        spdlog::error("[video_adapter] unsupported encoder output pixel format '{}'; "
                      "refusing config instead of silently encoding yuv420p",
                      opts.output_pix_fmt);
        return false;
    }
    config.encoder.encoded_pixel_format = *encoded_pixel_format;
    config.encoder.apply_gamma = opts.color_transform.apply_gamma;
    // The implemented pipe transports are synchronous single-frame writers:
    // the config must describe exactly what they do (queue_capacity=1,
    // write_timeout=0, io_uring/async off) or the validator would reject the
    // unsupported values. A requested io_uring writer is an explicit error,
    // not a silent fallback.
    config.transport.asynchronous = false;
    config.transport.queue_capacity = 1;
    if (opts.pipe_writer == "io_uring") {
        spdlog::error("[video_adapter] io_uring pipe writer is not implemented; "
                      "refusing config instead of silently writing synchronously");
        return false;
    }
    config.transport.use_io_uring = false;
    config.transport.write_timeout = std::chrono::milliseconds(0);
    config.output.container = sink_type_ == VideoSinkType::RawFile
        ? VideoContainer::Raw : detect_container(opts.output_path);
    config.output.output_path = std::filesystem::path(opts.output_path);
    config.output.overwrite = true;
    config.label = sink_type_ == VideoSinkType::RawFile
        ? "adapter-raw-video-sink" : "adapter-ffmpeg-sink";
    return true;
}

} // namespace chronon3d::cli
