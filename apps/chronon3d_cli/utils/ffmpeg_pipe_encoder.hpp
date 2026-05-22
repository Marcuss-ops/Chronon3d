#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <string>
#include <vector>

namespace chronon3d::cli {

enum class PipePixelFormat {
    RGBA,
    YUV420P,
    NV12
};

struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    int fps{30};
    int crf{18};
    std::string preset{"medium"};
    std::string codec{"libx264"};
    std::string output_path;
    PipePixelFormat input_format{PipePixelFormat::RGBA};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
    color::OutputTransformOptions color_transform{};
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
    [[nodiscard]] double total_write_blocked_ms() const { return total_write_blocked_ms_; }

    bool convert_framebuffer_to_rgba(const Framebuffer& fb);
    bool convert_framebuffer_to_yuv420p(const Framebuffer& fb);
    bool convert_framebuffer_to_nv12(const Framebuffer& fb);

private:
    FILE* pipe_{nullptr};
    FfmpegPipeOptions options_{};
    std::vector<uint8_t> rgba_buffer_;
    std::vector<uint8_t> y_plane_;
    std::vector<uint8_t> u_plane_;
    std::vector<uint8_t> v_plane_;
    std::vector<uint8_t> nv12_uv_plane_;
    uint64_t frames_written_{0};
    uint64_t bytes_written_{0};
    double total_write_blocked_ms_{0.0};
};

} // namespace chronon3d::cli
