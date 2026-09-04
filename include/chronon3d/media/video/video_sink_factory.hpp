#pragma once

// ---------------------------------------------------------------------------
// video_sink_factory.hpp — Factory function for creating built-in VideoSink
// instances.
//
// This is the sole public entry point for constructing Chronon3D's built-in
// sinks. Implementations are selected from VideoSinkConfig; the factory does
// not own a process-global extension registry.
//
// Callers should NOT include implementation-specific headers
// (e.g. ffmpeg_pipe_sink.hpp, native_av_sink.hpp). All concrete types are
// hidden behind the factory.
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

/// Create a built-in VideoSink implementation based on the configuration.
///
/// Selection logic:
///  - Uncompressed codec or Raw container -> RawVideoSink
///  - Compressed codec + native FFmpeg     -> NativeAvSink
///  - Compressed codec without native FFmpeg -> FfmpegPipeSink compatibility
///    fallback (tracked demolition debt; not a production authority)
///
/// The returned sink is in the Created state. Caller must invoke open()
/// before submit().
[[nodiscard]] std::unique_ptr<VideoSink> create_video_sink(
    const VideoSinkConfig& config);

} // namespace chronon3d::media::video
