#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    int fps{30};
    int crf{18};
    std::string preset{"medium"};
    std::string codec{"libx264"};
    std::string output_path;
    std::string input_pix_fmt{"rgba"};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
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

    [[nodiscard]] uint64_t frames_written() const { return frames_written_; }
    [[nodiscard]] uint64_t bytes_written() const { return bytes_written_; }
    [[nodiscard]] bool is_open() const { return pipe_ != nullptr; }

private:
    bool convert_framebuffer_to_rgba(const Framebuffer& fb);

    FILE* pipe_{nullptr};
    FfmpegPipeOptions options_{};
    std::vector<uint8_t> rgba_buffer_;
    uint64_t frames_written_{0};
    uint64_t bytes_written_{0};
};

} // namespace chronon3d::cli
