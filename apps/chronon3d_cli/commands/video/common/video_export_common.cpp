#include "video_export_common.hpp"
#include "../../../utils/video/encoder_config_resolution.hpp"
#include "../../../utils/video/video_sink_adapter.hpp"
#include "../../../utils/video/video_sink_encoders.hpp"
#include <chronon3d/runtime/frame_format.hpp>  // canonical pixel-format authority
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

// ── Factory: create the appropriate encoder based on sink_type / backend ──────

std::unique_ptr<IVideoEncoder> create_video_encoder(
    const FfmpegExportOptions& opts,
    const VideoEncoderCreateContext& context) {
    // Note: video_sink_type_id is set in setup_pipe_export_session() after the
    // renderer (and its counters) are created — g_current_counters is null here.

    switch (opts.sink.sink_type) {
        case VideoSinkType::NullRender:
            spdlog::info("[video] Using null-render sink — frames will be counted but not converted/written");
            return std::make_unique<NullRenderEncoder>();

        case VideoSinkType::NullConvert:
            spdlog::info("[video] Using null-convert sink — frames will be converted but not written to FFmpeg");
            return std::make_unique<NullConvertEncoder>();

        case VideoSinkType::RawFile:
            spdlog::info("[video] Using raw sink (new) — frames will be written as raw pixel data to {}", opts.output.output);
            return std::make_unique<VideoSinkEncoderAdapter>(VideoSinkType::RawFile);

        case VideoSinkType::Ffmpeg:
            break;
    }

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if (is_native_encoder_transport(opts.encoder.encoder_backend)) {
        return std::make_unique<NativeAvEncoder>(context.device_runtime);
    }
#else
    if (is_native_encoder_transport(opts.encoder.encoder_backend)) {
        // Native is an explicit execution contract. Do not silently change
        // the encoder authority to the external-process pipe path when this
        // build has no in-process libav support.
        spdlog::error(
            "[video] Native FFmpeg backend was requested, but this build has "
            "CHRONON3D_ENABLE_NATIVE_FFMPEG disabled; refusing pipe fallback");
        return nullptr;
    }
#endif
    spdlog::info("[video] Using in-process FFmpeg sink via VideoSinkEncoderAdapter");
    return std::make_unique<VideoSinkEncoderAdapter>(VideoSinkType::Ffmpeg);
}

std::optional<PipePixelFormat> try_parse_pipe_pixfmt(std::string_view fmt) noexcept {
    // ONE parser: the engine's pixel-format authority. The CLI enum is a
    // 4-value projection of the engine taxonomy, so the mapping is explicit
    // here and the *vocabulary* (which names exist) stays in one place.
    const auto parsed = runtime::try_parse_pixel_format(fmt);
    if (!parsed) return std::nullopt;
    switch (*parsed) {
        case runtime::PixelFormat::Rgba8Unorm: return PipePixelFormat::RGBA;
        case runtime::PixelFormat::Yuv420P:    return PipePixelFormat::YUV420P;
        case runtime::PixelFormat::Nv12:       return PipePixelFormat::NV12;
        case runtime::PixelFormat::P010:       return PipePixelFormat::P010;
        default:                               return std::nullopt;
    }
}


color::ColorSpace parse_color_output(const std::string& cs) {
    if (cs == "rec709") return color::ColorSpace::Rec709;
    if (cs == "linearsrgb") return color::ColorSpace::LinearSRGB;
    return color::ColorSpace::SRGB;
}

std::string resolve_cli_ffmpeg_codec(const std::string& codec, const std::string& hw_encoder) {
    if (hw_encoder == chronon3d::media::kNvencHardwareEncoder) {
        if (codec == "h264" || codec == "auto" || codec == "libx264") return "h264_nvenc";
        if (codec == "hevc" || codec == "libx265") return "hevc_nvenc";
    }
    if (codec != "auto") return codec;
    if (hw_encoder == "qsv") return "h264_qsv";
    if (hw_encoder == "videotoolbox" || hw_encoder == "vt") return "h264_videotoolbox";
    if (hw_encoder == "amf") return "h264_amf";
    return "libx264";
}

std::string resolve_cli_ffmpeg_output_pix_fmt(const std::string& codec) {
    if (codec == "libx264rgb") {
        return "rgb24";
    }
    if (codec == "libx264" || codec == "libx265") {
        // Use yuv420p for maximum compatibility with web and native players.
        return "yuv420p";
    }
    return "yuv420p";
}

} // namespace chronon3d::cli
