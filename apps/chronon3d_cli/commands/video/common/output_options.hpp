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
#include <chronon3d/core/types/time.hpp>

namespace chronon3d::cli {

/// Basic output options shared by all export paths.
struct OutputOptions {
    /// Output file path (.mp4, .mkv, .nut, or raw file).
    std::string output;

    /// Directory name for temporary/intermediate frames.
    std::string frames_dir_name;

    /// Output frame rate (frames per second).
    int fps{30};
    int fps_num{30};
    int fps_den{1};

    [[nodiscard]] chronon3d::FrameRate frame_rate() const noexcept {
        const int n = (fps_num == 30 && fps_den == 1 && fps != 30) ? fps : fps_num;
        return chronon3d::FrameRate{n, fps_den};
    }

    [[nodiscard]] double fps_value() const noexcept {
        const auto rate = frame_rate();
        return rate.denominator > 0
            ? static_cast<double>(rate.numerator) / rate.denominator : 0.0;
    }
};

} // namespace chronon3d::cli
