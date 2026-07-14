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
//
// Phase-2 (TICKET-FFMPEG-PIPE-SINK-SPLIT): the 4 private member methods
// (build_argv/launch_ffmpeg/write_to_pipe/validate_format) are extracted
// to the friend struct FfmpegPipeSinkInternal declared below.  External
// callers continue to use ONLY the public VideoSink interface (open/
// submit/submit_planar/submit_biplanar/flush/close + diagnostics +
// structured error reporting); the internal helper functions are
// accessible only via -internal.hpp opt-in from within the sibling
// implementation cpps.
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_config.hpp>

#include "process_runner.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::media::video {

// Forward declaration of the friend-struct access shim.  Defined in
// `ffmpeg_pipe_sink_internal.hpp`.  This PUBLIC header does NOT include
// -internal.hpp (preserves the SDK PUBLIC surface from leaking internal
// implementation signatures to external consumers); instead the friend
// declaration below grants the 4 private-member methods access to
// FfmpegPipeSink as friend access.
struct FfmpegPipeSinkInternal;

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

    // ── Structured error reporting (P1-C) ──────────────────────────────
    [[nodiscard]] VideoSinkError last_error() const noexcept override { return last_error_; }
    [[nodiscard]] std::string last_error_message() const noexcept override { return last_error_msg_; }

private:
    // ── Friend-struct access shim (Phase-2) ──────────────────────────────
    //
    // Per Phase-2 of TICKET-FFMPEG-PIPE-SINK-SPLIT: the 4 formerly-private
    // member methods (build_argv/launch_ffmpeg/write_to_pipe/validate_format)
    // are extracted to FfmpegPipeSinkInternal in `ffmpeg_pipe_sink_internal.hpp`.
    // The friend struct has access to FfmpegPipeSink's private members, so
    // the 4 methods can operate on `self.process_`/`self.state_`/etc. via the
    // friend decl below.
    //
    // ABI preserved: no public method removed, no signature changed, class
    // data layout IDENTICAL.  Per AGENTS.md §Cat-2 freeze: NO ADR required.
    friend struct FfmpegPipeSinkInternal;

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

    /// Structured error state (P1-C).
    VideoSinkError last_error_{VideoSinkError::None};
    std::string    last_error_msg_;
};

} // namespace chronon3d::media::video
