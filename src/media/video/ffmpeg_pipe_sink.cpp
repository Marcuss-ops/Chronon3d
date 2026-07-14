#include "ffmpeg_pipe_sink.hpp"

#include <chronon3d/media/video/video_frame.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace chronon3d::media::video {

// ============================================================================
//  launch_ffmpeg() — spawn via ProcessRunner (posix_spawnp, no shell)
//
//  Moved into this TU per P2 item #26: lifecycle/state orchestrator TU
//  (owns the ProcessRunner process_ member and the state_ state machine).
// ============================================================================

bool FfmpegPipeSink::launch_ffmpeg(const std::vector<std::string>& argv) {
    // argv[0] is "ffmpeg" — use as both executable name and argv[0].
    const bool ok = process_.launch(argv[0], argv);
    if (!ok) {
        last_error_ = VideoSinkError::FfmpegNotFound;
        last_error_msg_ = "failed to launch '" + argv[0] + "' — is ffmpeg on PATH?";
        state_ = VideoSinkState::Failed;
        return false;
    }
    return true;
}

// ============================================================================
//  write_to_pipe() — write raw bytes, track telemetry
// ============================================================================

bool FfmpegPipeSink::write_to_pipe(const uint8_t* data, size_t size) {
    if (pipe_failed_) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    // Always use write_for() — it handles O_NONBLOCK correctly via poll().
    // When write_timeout is 0 (no deadline), write_for() substitutes a
    // large default internally so the write never spuriously fails on
    // EAGAIN.
    const bool ok = process_.write_for(data, size, write_timeout_);

    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    total_write_blocked_ms_ += elapsed;

    if (!ok) {
        pipe_failed_ = true;
        last_error_ = VideoSinkError::PipeBroken;
        last_error_msg_ = "write_for() failed — pipe broken or child exited";
        state_ = VideoSinkState::Failed;
        return false;
    }

    stats_.bytes_written += size;
    return true;
}

// ============================================================================
//  open()
// ============================================================================

bool FfmpegPipeSink::open(const VideoSinkConfig& config) {
    if (state_ != VideoSinkState::Created) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Centralised config validation — reject invalid configurations early.
    const auto validation = validate_video_sink_config(config);
    if (!validation) {
        last_error_ = VideoSinkError::InvalidConfig;
        last_error_msg_ = validation.error_message.empty() ? "config validation failed" : validation.error_message;
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto& stream = config.stream;
    const auto fmt = config.stream.submitted_format;

    // Pre-check: if overwrite=false and file exists, fail early.
    // (The central validator doesn't check file existence — that's a
    //  runtime condition, not a configuration invariant.)
    if (!config.output.overwrite) {
        const auto output_path = config.output.output_path;
        if (std::filesystem::exists(output_path)) {
            last_error_ = VideoSinkError::FileExists;
            last_error_msg_ = output_path.string() + " already exists";
            state_ = VideoSinkState::Failed;
            return false;
        }
    }

    // Store session contract (the format callers submit frames in).
    width_ = stream.width;
    height_ = stream.height;
    session_format_ = fmt;
    tight_frame_size_ = frame_buffer_size(fmt, stream.width, stream.height);

    // Build argv and launch ffmpeg.
    const auto argv = build_argv(config);
    if (!launch_ffmpeg(argv)) {
        return false;
    }

    // Store the write timeout from config.
    write_timeout_ = config.transport.write_timeout;

    // Reset stats and error state.
    stats_ = Stats{};
    total_write_blocked_ms_ = 0.0;
    pipe_failed_ = false;
    last_error_ = VideoSinkError::None;
    last_error_msg_.clear();
    state_ = VideoSinkState::Open;
    return true;
}

// ============================================================================
//  flush()
// ============================================================================

bool FfmpegPipeSink::flush() {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    // fd-based writes have no userspace buffering; flush is effectively
    // a no-op.  We still track timing for telemetry parity.
    // The kernel pipe buffer is drained asynchronously by the child.

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_flush_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

// ============================================================================
//  close()
// ============================================================================

bool FfmpegPipeSink::close() noexcept {
    // Preserve the Failed state so it isn't overwritten to Closed below.
    // This matters when submit() set Failed (e.g. invalid frame format)
    // but the pipe itself is still intact — close() must still report failure.
    const bool was_failed = (state_ == VideoSinkState::Failed);

    if (state_ == VideoSinkState::Closed) {
        return true;
    }
    if (state_ == VideoSinkState::Created) {
        state_ = VideoSinkState::Closed;
        return true;
    }

    // Normal path: close stdin (signals EOF) and wait for ffmpeg to exit.
    // Use a generous 30s timeout — ffmpeg may need time to encode and mux
    // the last frames before its encoder flushes the output.
    if (!pipe_failed_) {
        const int exit_code = process_.wait_for(std::chrono::seconds(30));
        if (exit_code == -2) {
            // Timeout — escalate.
            process_.terminate_and_wait(std::chrono::seconds(5));
            const std::string stderr_log = process_.consume_stderr();
            last_error_ = VideoSinkError::Timeout;
            last_error_msg_ = "ffmpeg timed out after 30s";
            if (!stderr_log.empty()) {
                spdlog::warn("[FfmpegPipeSink] ffmpeg timed out — stderr:\n{}",
                             stderr_log);
                last_error_msg_ += " — stderr: " + stderr_log;
            }
            state_ = VideoSinkState::Failed;
            return false;
        }
        if (exit_code != 0) {
            const std::string stderr_log = process_.consume_stderr();
            last_error_ = VideoSinkError::EncoderFailed;
            last_error_msg_ = "ffmpeg exited with code " + std::to_string(exit_code);
            if (!stderr_log.empty()) {
                spdlog::error("[FfmpegPipeSink] ffmpeg exited with code {} — stderr:\n{}",
                              exit_code, stderr_log);
                last_error_msg_ += " — stderr: " + stderr_log;
            } else {
                spdlog::error("[FfmpegPipeSink] ffmpeg exited with code {} (no stderr)",
                              exit_code);
            }
            state_ = VideoSinkState::Failed;
            return false;
        }
    } else {
        // Pipe failed — terminate the child gracefully, then escalate.
        process_.terminate_and_wait(std::chrono::seconds(5));
        const std::string stderr_log = process_.consume_stderr();
        last_error_ = VideoSinkError::PipeBroken;
        last_error_msg_ = "pipe broken before close";
        if (!stderr_log.empty()) {
            spdlog::error("[FfmpegPipeSink] pipe broken — ffmpeg stderr:\n{}",
                          stderr_log);
            last_error_msg_ += " — stderr: " + stderr_log;
        }
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Restore Failed if it was set before close(), e.g. by a submit() that
    // rejected an invalid frame format without breaking the pipe.
    state_ = was_failed ? VideoSinkState::Failed : VideoSinkState::Closed;
    return !was_failed;
}

// ============================================================================
//  Destructor
// ============================================================================

FfmpegPipeSink::~FfmpegPipeSink() noexcept {
    // ProcessRunner destructor handles cleanup: close stdin, terminate,
    // wait for child.  Nothing extra needed here.
}

} // namespace chronon3d::media::video
