#pragma once

// ---------------------------------------------------------------------------
// raw_video_sink.hpp — Private implementation of VideoSink for raw pixel output.
//
// Writes raw (uncompressed) pixel data to a binary file with no container
// or encoding.  Each submit() appends the frame data sequentially.
//
// Lifecycle:
//   open(config) → submit(frame 0..N) → flush() → close()
//
// Thread-safety: NOT thread-safe.  The caller must serialise access.
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_config.hpp>

#include <fstream>
#include <vector>

namespace chronon3d::media::video {

/// Writes raw pixel data to a file (no encoding, no container).
///
/// Pixel format is determined by VideoSinkConfig::stream::submitted_format.
/// Each frame is appended as a tightly-packed byte sequence.
/// No headers, no metadata, no framing — purely raw pixel bytes.
///
/// Supported formats: RGBA8, YUV420P, NV12, RGB24.
///
/// Output file size: frames_submitted × frame_buffer_size(format, w, h).
class RawVideoSink final : public VideoSink {
public:
    RawVideoSink() noexcept = default;
    ~RawVideoSink() noexcept override;

    RawVideoSink(const RawVideoSink&) = delete;
    RawVideoSink& operator=(const RawVideoSink&) = delete;
    RawVideoSink(RawVideoSink&&) noexcept = default;
    RawVideoSink& operator=(RawVideoSink&&) noexcept = default;

    // ── VideoSink interface ─────────────────────────────────────────

    bool open(const VideoSinkConfig& config) override;
    bool submit(const VideoFrameView& frame) override;
    bool submit_planar(const PlanarVideoFrameView& frame) override;
    bool submit_biplanar(const BiplanarVideoFrameView& frame) override;
    bool flush() override;
    bool close() noexcept override;

    [[nodiscard]] VideoSinkState state() const noexcept override { return state_; }
    [[nodiscard]] std::string_view name() const noexcept override { return "raw-video-sink"; }
    [[nodiscard]] uint64_t frames_submitted() const noexcept override { return stats_.frames_submitted; }
    [[nodiscard]] Stats stats() const noexcept override { return stats_; }
    void reset_stats() noexcept override { stats_ = Stats{}; }

private:
    /// Internal file handle.
    std::ofstream file_;

    /// Current lifecycle state.
    VideoSinkState state_{VideoSinkState::Created};

    /// Staging buffer used for planar/biplanar → interleaved conversion.
    std::vector<uint8_t> staging_;

    /// Stride in bytes per row (computed from config during open()).
    std::size_t stride_{0};

    /// Session contract: dimensions and format set during open().
    /// Every submitted frame must match these exactly.
    int expected_width_{0};
    int expected_height_{0};
    PixelFormat expected_format_{};

    /// Cumulative statistics.
    Stats stats_;

    /// Write a byte span to the file.
    bool write_bytes(const uint8_t* data, size_t size);

    /// Validate that a frame matches the session contract.
    /// Returns true if valid; sets state to Failed and returns false otherwise.
    bool validate_frame_match(int width, int height, PixelFormat fmt) noexcept;
};

} // namespace chronon3d::media::video
