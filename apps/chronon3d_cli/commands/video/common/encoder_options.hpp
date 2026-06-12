#pragma once

// ---------------------------------------------------------------------------
// encoder_options.hpp
//
/// @file    encoder_options.hpp
/// @brief   Focused options for video encoder configuration.
///
/// Extracted from the monolithic FfmpegExportOptions to give encoder
/// settings their own identity, default values, and documentation.
// ---------------------------------------------------------------------------

#include <string>

namespace chronon3d::cli {

/// Encoder codec / quality settings.
///
/// Owned by VideoJobPlan (or FfmpegExportOptions for backward compat).
/// Constructed once during planning phase and read-only thereafter.
struct EncoderOptions {
    /// Video codec identifier (e.g. "auto", "libx264", "libx265", "mpeg4").
    std::string codec{"auto"};

    /// Hardware encoder backend ("none", "nvenc", "qsv", "videotoolbox", "amf").
    std::string hardware_encoder{"none"};

    /// Encoder preset (e.g. "superfast", "ultrafast", "medium", "slow").
    std::string encode_preset{"superfast"};

    /// Encoder tune parameter (e.g. "zerolatency", "film", "grain").
    std::string tune;

    /// Rate-control CRF value (0–51, lower = better quality).
    int crf{23};

    /// Encoder backend: "native" (in-process libavcodec) or "pipe" (external ffmpeg).
    std::string encoder_backend{"native"};
};

} // namespace chronon3d::cli
