#include "video_export_common.hpp"
#include "../../utils/video/native_av_encoder.hpp"
#include <cstdlib>

namespace chronon3d::cli {

// ── Factory: create the appropriate encoder based on backend setting ──────────

std::unique_ptr<IVideoEncoder> create_video_encoder(const FfmpegExportOptions& opts) {
    if (opts.encoder_backend == "native") {
        return std::make_unique<NativeAvEncoder>();
    }
    return std::make_unique<FfmpegPipeEncoder>();
}

bool ffmpeg_in_path() {
#ifdef _WIN32
    return std::system("ffmpeg -version > NUL 2>&1") == 0;
#else
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
#endif
}

PipePixelFormat parse_pipe_pixfmt(const std::string& fmt) {
    if (fmt == "yuv420p") return PipePixelFormat::YUV420P;
    if (fmt == "nv12")    return PipePixelFormat::NV12;
    return PipePixelFormat::RGBA;
}

color::ColorSpace parse_color_output(const std::string& cs) {
    if (cs == "rec709") return color::ColorSpace::Rec709;
    if (cs == "linearsrgb") return color::ColorSpace::LinearSRGB;
    return color::ColorSpace::SRGB;
}

std::string resolve_cli_ffmpeg_codec(const std::string& codec, const std::string& hw_encoder) {
    if (codec != "auto") return codec;
    if (hw_encoder == "nvenc") return "h264_nvenc";
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
