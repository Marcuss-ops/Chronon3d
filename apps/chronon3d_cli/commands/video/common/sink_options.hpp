#pragma once
#ifndef CHRONON3D_CLI_SINK_OPTIONS_HPP
#define CHRONON3D_CLI_SINK_OPTIONS_HPP

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

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

/// THE sink vocabulary. Every surface that must name the accepted set — the
/// `--video-sink` option checker, the job validator, tests — reads this list,
/// so an option can never advertise a name the parser rejects (the same
/// single-authority rule the pipe pixel format vocabulary follows).
[[nodiscard]] inline std::vector<std::string> video_sink_type_names() {
    return {"ffmpeg", "raw", "null-render", "null-convert"};
}

/// The accepted names as one comma-separated string. Error messages quote
/// this instead of re-typing a list that could drift from the vocabulary.
[[nodiscard]] inline std::string video_sink_names_joined() {
    std::string joined;
    for (const auto& name : video_sink_type_names()) {
        if (!joined.empty()) joined += ", ";
        joined += name;
    }
    return joined;
}

/// Non-throwing parse. This is the ONE parser: `parse_video_sink_type()` and
/// the job validator both go through it, so "is this name known?" cannot
/// diverge between the CLI flag check and the execution boundary.
[[nodiscard]] inline std::optional<VideoSinkType> try_parse_video_sink_type(
    std::string_view value) noexcept {
    if (value == "ffmpeg")       return VideoSinkType::Ffmpeg;
    if (value == "null-render")  return VideoSinkType::NullRender;
    if (value == "null-convert") return VideoSinkType::NullConvert;
    if (value == "raw")          return VideoSinkType::RawFile;
    return std::nullopt;
}

/// Parse a sink type from a CLI string value.
/// Accepted values: see video_sink_type_names().
[[nodiscard]] inline VideoSinkType parse_video_sink_type(const std::string& value) {
    const auto parsed = try_parse_video_sink_type(value);
    if (!parsed) {
        throw std::runtime_error("Invalid --video-sink: " + value +
                                 " (accepted: ffmpeg, raw, null-render, null-convert)");
    }
    return *parsed;
}

/// True only for the sinks that are executable end to end today.
///
/// `Ffmpeg` encodes and muxes; `RawFile` is the PRE-ENCODE truth surface: it
/// writes the rendered host frames (RGBA8 / the selected pipe format) with no
/// encoder and no container, which is what pixel-level text-quality diagnosis
/// must measure. The two null sinks exist for benchmarking only and are
/// deliberately refused by the job validator rather than run as an export.
[[nodiscard]] inline bool video_sink_type_is_executable(VideoSinkType type) noexcept {
    return type == VideoSinkType::Ffmpeg || type == VideoSinkType::RawFile;
}

/// Whether this sink runs an encoder/mux stage. A pre-encode sink never
/// requires an external ffmpeg, so capability checks must not demand one.
[[nodiscard]] inline bool video_sink_type_encodes(VideoSinkType type) noexcept {
    return type == VideoSinkType::Ffmpeg;
}

/// True when an EXPLICITLY REQUESTED sink name selects the video (frame)
/// output lane.
///
/// The sink is an output intention, so it — not the output file extension —
/// decides the lane: a pre-encode sink writes frames to a path that carries no
/// container meaning (`frames.raw`), and refusing it for "not being a video
/// output" would send it down the still/sequence image writer instead. An
/// empty name is not a request and leaves the extension in charge.
[[nodiscard]] inline bool video_sink_name_selects_video_lane(
    std::string_view name) noexcept {
    return !name.empty() && try_parse_video_sink_type(name).has_value();
}

/// Sink-specific options.
///
/// Most sinks are selected purely by type and don't need additional options.
/// FfmpegSink uses the native streaming `pipe` mode.
struct SinkOptions {
    /// The sink type selected for this export.
    VideoSinkType sink_type{VideoSinkType::Ffmpeg};

    /// FFmpeg sub-mode. The legacy PNG/chunked mode is no longer part of the
    /// CLI video target.
    /// Only meaningful when sink_type == Ffmpeg.
    std::string ffmpeg_mode{"pipe"};

    /// Keep temporary PNG frames after FFmpeg encode (chunked mode only).
    bool keep_frames{false};

    /// Number of parallel render chunks before encode (chunked mode only).
    int chunks{1};
};

} // namespace chronon3d::cli

#endif // CHRONON3D_CLI_SINK_OPTIONS_HPP
