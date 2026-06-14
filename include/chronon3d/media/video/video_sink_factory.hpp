#pragma once

// ---------------------------------------------------------------------------
// video_sink_factory.hpp — Factory function for creating VideoSink instances.
//
// This is the sole public entry point for constructing a VideoSink.
// Implementations are selected at runtime based on the VideoSinkConfig
// (specifically the output container and encoder settings).
//
// Callers should NOT include implementation-specific headers
// (e.g. ffmpeg_pipe_sink.hpp, native_av_sink.hpp).  All concrete types
// are hidden behind the factory.
//
// Usage:
//   auto sink = create_video_sink(config);
//   if (sink) {
//       sink->open(config);
//       sink->submit(frame);
//       sink->flush();
//       sink->close();
//   }
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_config.hpp>
#include <memory>

namespace chronon3d::media::video {

/// Create a VideoSink implementation based on the given configuration.
///
/// Selection logic:
///  - Uncompressed codec + Raw container  → RawVideoSink
///  - Compressed codec (default)          → FfmpegPipeSink
///  - Custom URI scheme                   → registered factory (if any)
///
/// Returns nullptr if no suitable implementation is available
/// (e.g. native FFmpeg not compiled in and no pipe fallback).
///
/// The returned sink is in the Created state.  Caller must invoke
/// open() before submit().
[[nodiscard]] std::unique_ptr<VideoSink> create_video_sink(
    const VideoSinkConfig& config);

/// Register a custom VideoSink factory for a given scheme/prefix.
///
/// This allows external plugins or tests to inject alternative
/// sink implementations without modifying the factory.
/// The `scheme` is matched against the output path's URI scheme
/// (e.g. "file", "http", "null", "test").
///
/// Thread-safety: NOT thread-safe.  Call once during initialisation.
using VideoSinkFactoryFn = std::unique_ptr<VideoSink>(*)(const VideoSinkConfig&);
void register_sink_factory(std::string_view scheme, VideoSinkFactoryFn factory);

/// Remove a previously registered factory.
void unregister_sink_factory(std::string_view scheme) noexcept;

} // namespace chronon3d::media::video
