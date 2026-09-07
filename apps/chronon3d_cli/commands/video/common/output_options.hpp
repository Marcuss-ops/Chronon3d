#pragma once

// ---------------------------------------------------------------------------
// output_options.hpp
//
/// @file    output_options.hpp
/// @brief   Focused options for video output path and frame rate.
///
/// Extracted from FfmpegExportOptions — these are the most basic settings
/// that every video export job needs regardless of sink type.
///
/// F3.1 — frame rate is rational-only: fps_num/fps_den are the SINGLE
/// authority. The legacy scalar `fps` alias was demolished (it used to
/// override the rational 30/1 default through canonical_frame_rate); the
/// `--fps` CLI flag now normalises into this rational at the CLI boundary.
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

    /// Output frame rate as an exact rational (numerator/denominator).
    /// fps_num/fps_den are the single authority — there is no scalar fps.
    int fps_num{30};
    int fps_den{1};

    [[nodiscard]] chronon3d::FrameRate frame_rate() const noexcept {
        return chronon3d::FrameRate{fps_num, fps_den};
    }

    [[nodiscard]] double fps_value() const noexcept {
        return fps_den > 0
            ? static_cast<double>(fps_num) / fps_den : 0.0;
    }
};

} // namespace chronon3d::cli
