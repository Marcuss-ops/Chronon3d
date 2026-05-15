#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <string>
#include <vector>
#include <memory>

namespace chronon3d::video {

struct VideoEncodeOptions {
    std::string codec{"auto"};
    std::string preset{"medium"};
    int crf{18};
    int fps{30};
    int gop_size{12};
    bool use_hardware_accel{false};
};

class FfmpegEncoder {
public:
    FfmpegEncoder();
    ~FfmpegEncoder();

    // Prevent copying
    FfmpegEncoder(const FfmpegEncoder&) = delete;
    FfmpegEncoder& operator=(const FfmpegEncoder&) = delete;

    // Support moving
    FfmpegEncoder(FfmpegEncoder&&) noexcept;
    FfmpegEncoder& operator=(FfmpegEncoder&&) noexcept;

    /**
     * Opens the encoder for writing to the specified path.
     * @return true if successful
     */
    bool open(const std::string& output_path, const VideoEncodeOptions& options, int width, int height);

    /**
     * Pushes a new frame to the encoder.
     * The framebuffer must match the dimensions provided in open().
     */
    bool push_frame(const Framebuffer& fb);

    /**
     * Flushes any remaining frames and closes the file.
     */
    void close();

    [[nodiscard]] bool is_open() const;

    // Static helpers for compatibility or one-shot encoding (if still needed)
    static bool is_available(); 

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace chronon3d::video
