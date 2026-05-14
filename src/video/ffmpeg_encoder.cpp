#include <chronon3d/video/ffmpeg_encoder.hpp>
#include <fmt/format.h>
#include <cstdlib>
#include <iostream>

namespace chronon3d::video {

bool FfmpegEncoder::is_available(const std::string& ffmpeg_path) {
#ifdef _WIN32
    std::string cmd = ffmpeg_path + " -version >nul 2>nul";
#else
    std::string cmd = ffmpeg_path + " -version >/dev/null 2>/dev/null";
#endif
    return std::system(cmd.c_str()) == 0;
}

std::string FfmpegEncoder::build_command(const std::string& frame_pattern,
                                         const std::string& output_path,
                                         const VideoEncodeOptions& options) {
    // Basic quoting for paths
    std::string cmd = fmt::format(
        "\"{}\" -y -framerate {} -i \"{}\" -c:v {} -pix_fmt {} -crf {} -preset {} {} \"{}\"",
        options.ffmpeg_path,
        options.fps,
        frame_pattern,
        options.codec,
        options.pix_fmt,
        options.crf,
        options.preset,
        options.extra_args.empty() ? "-movflags +faststart" : options.extra_args,
        output_path
    );
    return cmd;
}

int FfmpegEncoder::encode(const std::string& frame_pattern,
                          const std::string& output_path,
                          const VideoEncodeOptions& options) {
    std::string cmd = build_command(frame_pattern, output_path, options);
    return std::system(cmd.c_str());
}

} // namespace chronon3d::video
