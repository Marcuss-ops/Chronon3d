#include "ffmpeg_pipe_encoder.hpp"
#include <fmt/format.h>

namespace chronon3d::cli {
namespace {

std::string quote_path(const std::string& path) {
    std::string out = "\"";
    for (char c : path) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

std::string pix_fmt_to_ffmpeg_str(PipePixelFormat fmt) {
    switch (fmt) {
        case PipePixelFormat::RGBA:    return "rgba";
        case PipePixelFormat::YUV420P: return "yuv420p";
        case PipePixelFormat::NV12:    return "nv12";
    }
    return "rgba";
}

} // namespace

std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options) {
    const std::string log_flags = options.verbose
        ? ""
        : "-hide_banner -loglevel error ";

    return fmt::format(
        "ffmpeg -y "
        "{}"
        "-f rawvideo "
        "-pix_fmt {} "
        "-s {}x{} "
        "-r {} "
        "-i - "
        "-an "
        "-c:v {} "
        "-crf {} "
        "-preset {} "
        "-pix_fmt {} "
        "{}",
        log_flags,
        pix_fmt_to_ffmpeg_str(options.input_format),
        options.width,
        options.height,
        options.fps,
        options.codec,
        options.crf,
        options.preset,
        options.output_pix_fmt,
        quote_path(options.output_path)
    );
}

} // namespace chronon3d::cli
