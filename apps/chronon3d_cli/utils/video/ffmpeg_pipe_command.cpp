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
    return "rgba";
}

} // namespace

std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options) {
    const std::string log_flags = options.verbose
        ? ""
        : "-hide_banner -loglevel error ";

    const bool rgb_output = options.codec == "libx264rgb";
    const std::string output_pix_fmt = rgb_output ? "rgb24" : "yuv420p";
    const std::string colorspace_flags = rgb_output
        ? ""
        : "-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv ";

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
        "{}"
        "-movflags +faststart "
        "{}",
        log_flags,
        pix_fmt_to_ffmpeg_str(options.input_format),
        options.width,
        options.height,
        options.fps,
        options.codec,
        options.crf,
        options.preset,
        output_pix_fmt,
        colorspace_flags,
        quote_path(options.output_path)
    );
}

} // namespace chronon3d::cli
