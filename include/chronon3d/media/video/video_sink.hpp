#pragma once

// ---------------------------------------------------------------------------
// video_sink.hpp — Abstract VideoSink interface.
//
// A VideoSink consumes raw video frames and produces encoded output.
// It is the core abstraction of the video export pipeline:
//
//   Renderer → FrameConverter → VideoSink → encoded file
//
// Implementations:
//  - FfmpegPipeSink: pipes raw frames to an external FFmpeg subprocess
//  - NativeAvSink:   in-process encoding via libavcodec/libavformat
//  - RawVideoSink:   writes raw pixel data to a file (no encoding)
//  - NullSink:       discards all frames (for benchmarking)
//
// Lifecycle:
//   open(config) → submit(frame 0..N) → flush() → close()
//
// Contracts:
//  - submit() after close() returns false (error)
//  - close() is idempotent
//  - flush() blocks until all accepted frames are fully processed
//  - An I/O or encoder error transitions state to Failed
//  - The destructor must NOT throw exceptions
//  - No accepted frame may be silently dropped
//  - Writer-thread errors are propagated to the submit() caller
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_frame.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace chronon3d::media::video {

/// Lifecycle state of a VideoSink.
enum class VideoSinkState : uint8_t {
    /// Initial state after construction.  Must call open() before submit().
    Created = 0,

    /// Ready to accept frames via submit().  Set after successful open().
    Open = 1,

    /// Currently flushing buffered frames.  submit() returns false during
    /// this state.  Transitions back to Open or to Failed after flush completes.
    Flushing = 2,

    /// Successfully closed.  Terminal state.  No further operations allowed.
    /// Stats are preserved for telemetry after close().
    Closed = 3,

    /// An error occurred (I/O, encoder, broken pipe, etc.).
    /// The sink is in an unusable state and must be destroyed.
    Failed = 4,
};

/// Returns a human-readable name for the state.
[[nodiscard]] inline std::string_view to_string(VideoSinkState s) noexcept {
    switch (s) {
        case VideoSinkState::Created:  return "Created";
        case VideoSinkState::Open:     return "Open";
        case VideoSinkState::Flushing: return "Flushing";
        case VideoSinkState::Closed:   return "Closed";
        case VideoSinkState::Failed:   return "Failed";
    }
    return "Unknown";
}

/// Abstract interface for a video output sink.
///
/// Thread-safety: Implementations are NOT required to be thread-safe
/// unless explicitly documented.  Callers must serialise access.
class VideoSink {
public:
    virtual ~VideoSink() noexcept = default;

    /// Open the sink with the given configuration.
    /// Must be called before any submit() calls.
    /// Returns true on success, false on error (state transitions to Failed).
    virtual bool open(const class VideoSinkConfig& config) = 0;

    /// Submit a single video frame for encoding/writing.
    /// The data pointer must remain valid until submit() returns.
    /// After return, the sink has consumed the data.
    ///
    /// Returns true on success, false on error.
    /// On error, state transitions to Failed.
    virtual bool submit(const VideoFrameView& frame) = 0;

    /// Submit a planar (YUV420P) frame with separate Y/U/V planes.
    /// Implementations should convert to the internal format as needed.
    virtual bool submit_planar(const PlanarVideoFrameView& frame) = 0;

    /// Submit a biplanar (NV12) frame with separate Y and interleaved UV planes.
    /// Implementations should convert to the internal format as needed.
    virtual bool submit_biplanar(const BiplanarVideoFrameView& frame) = 0;

    /// Block until all accepted frames have been fully processed
    /// (encoded, written to pipe/file, etc.).
    /// Returns true on success, false if any frame failed during flush.
    /// After flush(), the sink is ready for close().
    virtual bool flush() = 0;

    /// Close the sink and release all resources.
    /// Idempotent: calling close() on an already-closed sink is a no-op.
    /// Must NOT throw exceptions.
    /// Returns true on success, false if the finalisation failed
    /// (e.g. encoder trailer write failed).
    virtual bool close() noexcept = 0;

    /// Returns the current lifecycle state.
    [[nodiscard]] virtual VideoSinkState state() const noexcept = 0;

    /// Human-readable name of the sink implementation (e.g. "ffmpeg-pipe").
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Total number of frames successfully submitted.
    /// Resets on successful open() or reset_stats().
    /// Persists after close() for telemetry consumption.
    [[nodiscard]] virtual uint64_t frames_submitted() const noexcept = 0;

    // ── Diagnostics ────────────────────────────────────────────────────

    struct Stats {
        uint64_t frames_submitted{0};
        uint64_t bytes_written{0};
        double   total_submit_ms{0.0};  // cumulative wall time in submit()
        double   total_flush_ms{0.0};   // cumulative wall time in flush()
        uint64_t submit_count{0};       // number of submit() calls
    };

    /// Returns cumulative statistics since the last open().
    [[nodiscard]] virtual Stats stats() const noexcept = 0;

    /// Reset cumulative statistics (without changing state).
    virtual void reset_stats() noexcept = 0;

    // ── Diagnostics ────────────────────────────────────────────────────

    struct Diagnostics {
        /// Child process PID (-1 if not applicable or not running).
        int child_pid{-1};

        /// Total time blocked on writes (ms).
        double blocked_write_ms{0.0};

        /// Backend identifier (e.g. "ffmpeg-pipe", "raw-file").
        std::string_view backend;
    };

    /// Return runtime diagnostics from the sink implementation.
    [[nodiscard]] virtual Diagnostics diagnostics() const noexcept {
        return {};
    }
};

} // namespace chronon3d::media::video
