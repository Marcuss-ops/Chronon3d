#pragma once

// ---------------------------------------------------------------------------
// output_options.hpp
//
/// @file    output_options.hpp
/// @brief   Focused options for video output path and frame rate.
///
/// Extracted from FfmpegExportOptions — these are the most basic settings
/// that every video export job needs regardless of sink type.
// ---------------------------------------------------------------------------

#include <string>

namespace chronon3d::cli {

/// Basic output options shared by all export paths.
struct OutputOptions {
    /// Output file path (.mp4, .mkv, .nut, or raw file).
    std::string output;

    /// Directory name for temporary/intermediate frames.
    std::string frames_dir_name;

    /// Output frame rate (frames per second).
    int fps{30};
};

} // namespace chronon3d::cli
