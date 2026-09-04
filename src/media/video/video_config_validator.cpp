// ---------------------------------------------------------------------------
// video_config_validator.cpp — Centralised VideoSinkConfig validation.
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_config.hpp>

#include <cstdint>
#include <string>

namespace chronon3d::media::video {
namespace {

[[nodiscard]] const char* check_yuv_dimensions(
    PixelFormat fmt, int w, int h) noexcept
{
    if ((fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) &&
        (w % 2 != 0 || h % 2 != 0)) {
        return "YUV420P and NV12 require even width and height";
    }
    return nullptr;
}

[[nodiscard]] const char* check_codec_container(
    VideoCodec codec, VideoContainer container) noexcept
{
    if (container == VideoContainer::WebM) {
        if (codec == VideoCodec::H264 || codec == VideoCodec::H264Nvenc) {
            return "WebM container does not support H.264 codec";
        }
        if (codec == VideoCodec::H265) {
            return "WebM container does not support H.265/HEVC codec";
        }
        if (codec == VideoCodec::Uncompressed) {
            return "WebM container does not support uncompressed/raw codec";
        }
        return nullptr;
    }

    if (container == VideoContainer::Raw) {
        if (codec != VideoCodec::Uncompressed) {
            return "Raw container requires Uncompressed codec; "
                   "use VideoCodec::Uncompressed";
        }
        return nullptr;
    }

    if (container == VideoContainer::Mp4) {
        if (codec == VideoCodec::VP9) {
            return "MP4 container with VP9 codec is unusual and may not "
                   "be playable everywhere; use MKV or WebM instead";
        }
        if (codec == VideoCodec::AV1) {
            return "MP4 container with AV1 codec requires a recent player; "
                   "consider MKV instead";
        }
    }

    if (codec == VideoCodec::Uncompressed && container != VideoContainer::Raw) {
        return "Uncompressed codec requires Raw container";
    }
    return nullptr;
}

} // anonymous namespace

ValidationResult validate_video_sink_config(
    const VideoSinkConfig& config) noexcept
{
    const auto& stream = config.stream;
    if (stream.width <= 0) return {false, "stream.width must be > 0"};
    if (stream.height <= 0) return {false, "stream.height must be > 0"};
    if (stream.width > kMaxFrameDimension) {
        return {false, "stream.width exceeds max dimension (16384)"};
    }
    if (stream.height > kMaxFrameDimension) {
        return {false, "stream.height exceeds max dimension (16384)"};
    }
    const int64_t pixels = static_cast<int64_t>(stream.width) *
                           static_cast<int64_t>(stream.height);
    if (pixels > kMaxPixelCount) {
        return {false, "width*height exceeds max pixel count (268M)"};
    }
    if (stream.frame_rate_num <= 0) {
        return {false, "stream.frame_rate_num must be > 0"};
    }
    if (stream.frame_rate_den <= 0) {
        return {false, "stream.frame_rate_den must be > 0"};
    }
    if (const char* err = check_yuv_dimensions(
            stream.submitted_format, stream.width, stream.height)) {
        return {false, err};
    }

    const auto& enc = config.encoder;
    if (enc.crf < -1 || enc.crf > 51) {
        return {false, "encoder.crf must be in [-1, 51] (-1 = codec default)"};
    }
    if (enc.qp < -1 || enc.qp > 63) {
        return {false, "encoder.qp must be in [-1, 63] (-1 = codec default)"};
    }
    if (enc.bitrate < 0) {
        return {false, "encoder.bitrate must be >= 0"};
    }

    if (enc.codec != VideoCodec::Uncompressed) {
        switch (enc.rate_control_mode) {
        case RateControlMode::Crf:
            if (enc.qp >= 0 || enc.bitrate > 0) {
                return {false, "CRF mode cannot be combined with QP or bitrate"};
            }
            break;
        case RateControlMode::ConstantQp:
            if (enc.crf >= 0 || enc.bitrate > 0) {
                return {false, "ConstantQp mode cannot be combined with CRF or bitrate"};
            }
            if (enc.qp < 0) {
                return {false, "ConstantQp mode requires encoder.qp"};
            }
            break;
        case RateControlMode::Bitrate:
            if (enc.crf >= 0 || enc.qp >= 0) {
                return {false, "Bitrate mode cannot be combined with CRF or QP"};
            }
            if (enc.bitrate == 0) {
                return {false, "Bitrate mode requires encoder.bitrate > 0"};
            }
            break;
        }
    }

    const auto resolved_codec = resolve_auto_codec(
        enc.codec, config.output.container);
    if (const char* err = check_codec_container(
            resolved_codec, config.output.container)) {
        return {false, err};
    }

    if (config.output.output_path.empty()) {
        return {false, "output.output_path must not be empty"};
    }
    if (config.transport.asynchronous) {
        return {false, "transport.asynchronous=true is not yet implemented; "
                       "set transport.asynchronous=false for synchronous mode"};
    }
    if (config.transport.write_timeout.count() < 0) {
        return {false, "transport.write_timeout must be >= 0ms"};
    }
    return {true, {}};
}

} // namespace chronon3d::media::video
