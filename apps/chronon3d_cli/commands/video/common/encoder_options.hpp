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

#include <cstdint>
#include <string>

namespace chronon3d::cli {

/// Encoder codec / quality settings.
///
/// Owned by FfmpegExportOptions.
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

    /// Rate-control mode: crf, qp, or bitrate.
    std::string rate_control_mode{"crf"};

    /// CRF value (0–51), used when rate_control_mode=crf.
    int crf{23};

    /// Constant QP value (0–63), used when rate_control_mode=qp.
    int qp{-1};

    /// Target bitrate in bits/second, used when rate_control_mode=bitrate.
    std::int64_t bitrate{0};

    /// Encoder backend: "native" (in-process libavcodec) or "pipe" (external ffmpeg).
    std::string encoder_backend{"native"};
};

} // namespace chronon3d::cli
