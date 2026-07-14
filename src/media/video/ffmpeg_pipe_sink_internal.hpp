#pragma once

// ============================================================================
// ffmpeg_pipe_sink_internal.hpp — Friend-struct access shim for FfmpegPipeSink
//
// Per Phase-2 of TICKET-FFMPEG-PIPE-SINK-SPLIT: the 4 (not 5) private member
// methods of FfmpegPipeSink (build_argv + launch_ffmpeg + write_to_pipe +
// validate_format) are extracted via the C++ `friend struct` pattern into
// STATIC FUNCTIONS of this header. The PUBLIC class surface remains
// UNCHANGED — only the private visibility is replaced with a
// `friend struct FfmpegPipeSinkInternal;` declaration in
// ffmpeg_pipe_sink.hpp.
//
// Why this pattern (vs PIMPL):
// - PIMPL (`std::unique_ptr<Impl> impl_`) WOULD change object memory
//   layout, which is an ABI break per AGENTS.md §Cat-2 freeze source-removal
//   gate. The friend-struct approach preserves the PUBLIC class data-layout
//   IDENTICAL — per Cat-2 freeze NO ADR is required.
// - C++ ODR (§3.2/4) prohibits splitting a single class definition across
//   two headers. The friend-struct pattern keeps the class definition
//   ENTIRE in the public header, while moving the IMPLEMENTATION SIGNATURE
//   surface to this internal header. Each method is now a static
//   non-virtual function on the friend struct, callable from the 3 sibling
//   implementation cpps.
//
// These methods are NOT part of the SDK PUBLIC API. They are accessed by:
//   - src/media/video/ffmpeg_pipe_args.cpp     (build_argv)
//   - src/media/video/ffmpeg_frame_submit.cpp  (validate_format + write_to_pipe calls)
//   - src/media/video/ffmpeg_pipe_sink.cpp     (launch_ffmpeg + write_to_pipe calls)
// External SDK consumers MUST NOT use these directly — they are
// internal-only and not exposed via the install-time SDK surface.
// ============================================================================

#include "ffmpeg_pipe_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::media::video {

/// Friend-struct access shim for FfmpegPipeSink's 4 private member methods.
///
/// Each static method corresponds exactly to a removed private member method
/// of FfmpegPipeSink. Methods that previously accessed `this->state_` /
/// `this->process_` / etc. now take a `FfmpegPipeSink& self` (or
/// `const FfmpegPipeSink& self` for `validate_format`) parameter.
///
/// Call-site migration:
///   Before: `this->write_to_pipe(data, size)`
///   After:  `FfmpegPipeSinkInternal::write_to_pipe(*this, data, size)`
///
///   Before: `this->validate_format(frame)`
///   After:  `FfmpegPipeSinkInternal::validate_format(*this, frame)`
///
///   Before: `this->launch_ffmpeg(argv)`
///   After:  `FfmpegPipeSinkInternal::launch_ffmpeg(*this, argv)`
///
///   Before: `this->build_argv(config)`
///   After:  `FfmpegPipeSinkInternal::build_argv(config)` (no self needed, pure data → data)
struct FfmpegPipeSinkInternal {
    /// Build the ffmpeg argument vector from config.
    /// Migrated from `FfmpegPipeSink::build_argv`. Pure data-to-data; no self state.
    /// Returns {executable, arg0, arg1, ...}.
    static std::vector<std::string> build_argv(const VideoSinkConfig& config);

    /// Launch the ffmpeg subprocess via ProcessRunner.
    /// Migrated from `FfmpegPipeSink::launch_ffmpeg`. Mutates self.process_ +
    /// self.last_error_ + self.state_.
    static bool launch_ffmpeg(FfmpegPipeSink& self, const std::vector<std::string>& argv);

    /// Write raw bytes to the child's stdin, blocking until complete.
    /// Migrated from `FfmpegPipeSink::write_to_pipe`. Mutates self.pipe_failed_ +
    /// self.stats_.bytes_written + self.total_write_blocked_ms_ +
    /// self.last_error_ + self.state_.
    static bool write_to_pipe(FfmpegPipeSink& self, const uint8_t* data, size_t size);

    /// Validate that the frame format matches the session contract.
    /// Migrated from `FfmpegPipeSink::validate_format`. Reads self.session_format_ +
    /// self.width_ + self.height_.
    static bool validate_format(const FfmpegPipeSink& self, const VideoFrameView& frame) noexcept;
};

} // namespace chronon3d::media::video
