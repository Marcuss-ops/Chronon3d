#pragma once

// ---------------------------------------------------------------------------
// pipe_options.hpp
//
/// @file    pipe_options.hpp
/// @brief   Focused options for the FFmpeg pipe subprocess mode.
///
/// Extracted from FfmpegExportOptions — these settings are only relevant
/// when the export sink is FfmpegSink in pipe mode.
// ---------------------------------------------------------------------------

#include <string>

namespace chronon3d::cli {

/// Pipe-mode specific options (ignored for non-pipe sinks).
///
/// Controls pixel format, pipe writer backend, colour space conversion,
/// and FFmpeg subprocess verbosity.
struct PipeOptions {
    /// Pixel format for pipe input: "rgba", "yuv420p", "nv12", "yuv444p".
    std::string pipe_pixfmt{"rgba"};

    /// Pipe writer backend: "classic" (stdio) or "io_uring" (Linux experimental).
    std::string pipe_writer{"classic"};

    /// Output colour space: "srgb", "rec709", "linearsrgb".
    std::string color_output{"srgb"};

    /// Show FFmpeg stderr logs in pipe mode.
    bool ffmpeg_verbose{false};
};

} // namespace chronon3d::cli
