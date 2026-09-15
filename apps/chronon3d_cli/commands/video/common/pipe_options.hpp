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
    /// Pixel format for pipe input. The accepted names are NOT restated here:
    /// chronon3d::runtime::pipe_pixel_format_names() in
    /// <chronon3d/runtime/frame_format.hpp> is the one vocabulary, projected for
    /// the CLI by pipe_pixfmt_vocabulary.hpp. (The list that used to sit in this
    /// comment named "yuv444p", which the canonical parser rejects, and omitted
    /// "p010", which it accepts.)
    std::string pipe_pixfmt{"rgba"};

    /// Pipe writer backend: "classic" (stdio) or "io_uring" (Linux experimental).
    std::string pipe_writer{"classic"};

    /// Output colour space: "srgb", "rec709", "linearsrgb".
    std::string color_output{"srgb"};

    /// Show FFmpeg stderr logs in pipe mode.
    bool ffmpeg_verbose{false};
};

} // namespace chronon3d::cli
