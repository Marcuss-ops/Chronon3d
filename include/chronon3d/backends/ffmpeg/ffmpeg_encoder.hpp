#pragma once

#include <chronon3d/backends/video/video_encoder.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::video {

class FfmpegEncoder final : public IEncoder {
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
    bool open(const std::string& output_path, const VideoEncodeOptions& options, int width, int height) override;

    /**
     * Pushes a new frame to the encoder.
     * The framebuffer must match the dimensions provided in open().
     */
    bool push_frame(const Framebuffer& fb) override;

    /**
     * Flushes any remaining frames and closes the file.
     */
    void close() override;

    [[nodiscard]] bool is_open() const override;

    // Static helpers for compatibility or one-shot encoding (if still needed)
    static bool is_available(); 

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace chronon3d::video
