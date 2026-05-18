#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <string>

namespace chronon3d::cli {

struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    int fps{30};
    int crf{18};
    std::string preset{"medium"};
    std::string codec{"libx264"};
    std::string output_path;
};

std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options);

class FfmpegPipeEncoder {
public:
    FfmpegPipeEncoder() = default;
    ~FfmpegPipeEncoder();

    FfmpegPipeEncoder(const FfmpegPipeEncoder&) = delete;
    FfmpegPipeEncoder& operator=(const FfmpegPipeEncoder&) = delete;

    bool open(const FfmpegPipeOptions& options);
    bool write_frame(const Framebuffer& fb);
    bool close();

private:
    FILE* pipe_{nullptr};
    FfmpegPipeOptions options_{};
};

} // namespace chronon3d::cli
