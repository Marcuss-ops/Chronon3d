#pragma once

// ---------------------------------------------------------------------------
// sink_options.hpp
//
/// @file    sink_options.hpp
/// @brief   Sink type selection and sink-specific options.
///
/// Introduces VideoSinkType enum and SinkOptions struct so that the
/// export pipeline can be configured for different output targets
/// (FFmpeg encode, raw file, null/benchmark) without branching on
/// a flat string field.
// ---------------------------------------------------------------------------

#include <string>

namespace chronon3d::cli {

/// Identifies which output sink the export pipeline uses.
///
/// The sink is selected at planning time from CLI args and drives
/// which VideoExporter implementation is chosen from the registry.
enum class VideoSinkType {
    /// Render → encode via FFmpeg (pipe subprocess or native libav).
    Ffmpeg = 0,

    /// Render + convert pixels but discard output (benchmark render path).
    NullRender = 1,

    /// Render + convert + encode but discard output (benchmark full path).
    NullConvert = 2,

    /// Render → write raw RGBA/PNG frames to disk (no encode step).
    RawFile = 3,
};

/// Returns a stable string identifier for the sink type (e.g. "ffmpeg", "raw").
[[nodiscard]] inline std::string_view to_string(VideoSinkType t) {
    switch (t) {
        case VideoSinkType::Ffmpeg:     return "ffmpeg";
        case VideoSinkType::RawFile:    return "raw";
        case VideoSinkType::NullRender: return "null-render";
        case VideoSinkType::NullConvert: return "null-convert";
    }
    return "unknown";
}

/// Parse a sink type from a CLI string value.
/// Accepted values: "ffmpeg", "null-render", "null-convert", "raw".
[[nodiscard]] inline VideoSinkType parse_video_sink_type(const std::string& value) {
    if (value == "ffmpeg")       return VideoSinkType::Ffmpeg;
    if (value == "null-render")  return VideoSinkType::NullRender;
    if (value == "null-convert") return VideoSinkType::NullConvert;
    if (value == "raw")          return VideoSinkType::RawFile;
    throw std::runtime_error("Invalid --video-sink: " + value);
}

/// Sink-specific options.
///
/// Most sinks are selected purely by type and don't need additional options.
/// FfmpegSink still uses `ffmpeg_mode` ("pipe" or "png") and its related
/// chunking / keep-frames behaviour.
struct SinkOptions {
    /// The sink type selected for this export.
    VideoSinkType sink_type{VideoSinkType::Ffmpeg};

    /// FFmpeg sub-mode: "pipe" (stream via pipe) or "png" (render → PNG → ffmpeg).
    /// Only meaningful when sink_type == Ffmpeg.
    std::string ffmpeg_mode{"pipe"};

    /// Keep temporary PNG frames after FFmpeg encode (chunked mode only).
    bool keep_frames{false};

    /// Number of parallel render chunks before encode (chunked mode only).
    int chunks{1};
};

} // namespace chronon3d::cli
