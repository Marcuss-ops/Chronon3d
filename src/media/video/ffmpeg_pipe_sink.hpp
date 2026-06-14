#pragma once

// ---------------------------------------------------------------------------
// ffmpeg_pipe_sink.hpp — VideoSink that pipes frames to an FFmpeg subprocess.
//
// Launches ffmpeg as a child process connected via a pipe.  Raw pixel data
// is written to the pipe in the submitted format; ffmpeg handles encoding
// and muxing to the configured output pixel format.
//
// Does NOT depend on CLI-level render loops or exporter orchestration.
// Uses posix_spawnp (Linux/macOS) / CreateProcessW (Windows) for shell-free
// subprocess execution with real PID tracking and exit code collection.
//
// Thread-safety: NOT thread-safe.  The caller must serialise access.
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_config.hpp>

#include "process_runner.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::media::video {

/// Pipes raw video frames to an external FFmpeg subprocess for encoding.
///
/// Lifecycle:  Created → open() → submit()×N → flush() → close() → Closed
///
/// Format: frames are submitted in the stream's submitted_format (set during
/// open()).  Planar/biplanar frames are tight-packed into interleaved
/// layout before writing to the pipe.  Format conversion (Framebuffer →
/// VideoFrameView) happens upstream, outside this sink.
class FfmpegPipeSink final : public VideoSink {
public:
    FfmpegPipeSink() noexcept = default;
    ~FfmpegPipeSink() noexcept override;

    FfmpegPipeSink(const FfmpegPipeSink&) = delete;
    FfmpegPipeSink& operator=(const FfmpegPipeSink&) = delete;
    FfmpegPipeSink(FfmpegPipeSink&&) noexcept = default;
    FfmpegPipeSink& operator=(FfmpegPipeSink&&) noexcept = default;

    // ── VideoSink interface ─────────────────────────────────────────────

    bool open(const VideoSinkConfig& config) override;
    bool submit(const VideoFrameView& frame) override;
    bool submit_planar(const PlanarVideoFrameView& frame) override;
    bool submit_biplanar(const BiplanarVideoFrameView& frame) override;
    bool flush() override;
    bool close() noexcept override;

    [[nodiscard]] VideoSinkState      state() const noexcept override { return state_; }
    [[nodiscard]] std::string_view    name() const noexcept override { return "ffmpeg-pipe-sink"; }
    [[nodiscard]] uint64_t            frames_submitted() const noexcept override { return stats_.frames_submitted; }
    [[nodiscard]] Stats               stats() const noexcept override { return stats_; }
    void                              reset_stats() noexcept override { stats_ = Stats{}; }

    // ── Diagnostics ─────────────────────────────────────────────────────

    /// PID of the ffmpeg child process (valid after successful open()).
    [[nodiscard]] int ffmpeg_pid() const noexcept { return process_.pid(); }

    /// Total wall-clock time spent blocked on pipe writes (ms).
    [[nodiscard]] double total_write_blocked_ms() const noexcept { return total_write_blocked_ms_; }

    /// Return diagnostics: PID, blocked write time, backend name.
    [[nodiscard]] Diagnostics diagnostics() const noexcept override {
        return Diagnostics{
            .child_pid        = process_.pid(),
            .blocked_write_ms = total_write_blocked_ms_,
            .backend          = "ffmpeg-pipe",
        };
    }

private:
    // ── Subprocess management ──────────────────────────────────────────

    /// Build the ffmpeg argument vector from config.
    /// Returns {executable, arg0, arg1, ...}.
    static std::vector<std::string> build_argv(const VideoSinkConfig& config);

    /// Launch the ffmpeg subprocess via ProcessRunner.
    bool launch_ffmpeg(const std::vector<std::string>& argv);

    /// Write raw bytes to the child's stdin, blocking until complete.
    bool write_to_pipe(const uint8_t* data, size_t size);

    // ── Conversion ──────────────────────────────────────────────────────

    /// Validate that the frame format matches the session contract.
    bool validate_format(const VideoFrameView& frame) const noexcept;

    // ── State ───────────────────────────────────────────────────────────

    ProcessRunner             process_;          // no-shell ffmpeg subprocess
    VideoSinkState            state_{VideoSinkState::Created};
    Stats                     stats_;

    // Session contract (set during open()).
    int                       width_{0};
    int                       height_{0};
    PixelFormat               session_format_{PixelFormat::RGBA8};
    size_t                    tight_frame_size_{0};

    /// Staging buffer for interleaved/packed frames (reused across calls).
    std::vector<uint8_t>      staging_;

    bool                      pipe_failed_{false};
    double                    total_write_blocked_ms_{0.0};

    /// Per-frame write deadline (from config.transport.write_timeout).
    /// 0ms = no timeout (blocking write).
    std::chrono::milliseconds write_timeout_{30000};
};

} // namespace chronon3d::media::video
