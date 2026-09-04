#include <chronon3d/media/video/video_sink_factory.hpp>

#include "raw_video_sink.hpp"
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "encode/native_av_sink.hpp"
#else
#include "ffmpeg_pipe_sink.hpp"
#endif

namespace chronon3d::media::video {

namespace {

/// Select a built-in sink implementation based on config.
///
/// Video sink selection is deliberately not backed by a process-global
/// registry. Extension-owned factories must live in host-owned catalogs;
/// this factory only owns Chronon3D's built-in sink selection.
[[nodiscard]] std::unique_ptr<VideoSink> create_builtin_sink(
    const VideoSinkConfig& config)
{
    const auto codec = resolve_auto_codec(
        config.encoder.codec, config.output.container);

    // Raw/uncompressed -> RawVideoSink (write raw pixel data to file).
    if (codec == VideoCodec::Uncompressed ||
        config.output.container == VideoContainer::Raw) {
        return std::make_unique<RawVideoSink>();
    }

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    // Production compressed output is always in-process when the native
    // FFmpeg backend is available. libavcodec owns codec mechanics and the
    // sink delegates packet/container work to MuxSession/libavformat.
    return std::make_unique<NativeAvSink>();
#else
    // Compatibility-only subprocess fallback.
    // Demolition debt: docs/tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md
    // defines the exit conditions after which this branch and its process
    // runner/pipe implementation are deleted rather than retained forever.
    return std::make_unique<FfmpegPipeSink>();
#endif
}

} // anonymous namespace

std::unique_ptr<VideoSink> create_video_sink(const VideoSinkConfig& config) {
    return create_builtin_sink(config);
}

} // namespace chronon3d::media::video
