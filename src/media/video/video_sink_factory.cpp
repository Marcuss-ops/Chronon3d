#include <chronon3d/media/video/video_sink_factory.hpp>

#include "raw_video_sink.hpp"
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "encode/native_av_sink.hpp"
#endif

namespace chronon3d::media::video {

namespace {

/// Select a built-in sink implementation based on config.
///
/// Raw/uncompressed output remains available in every video build. Compressed
/// output has exactly one authority: NativeAvSink. There is deliberately no
/// subprocess compatibility fallback when native FFmpeg support is disabled.
[[nodiscard]] std::unique_ptr<VideoSink> create_builtin_sink(
    const VideoSinkConfig& config)
{
    const auto codec = resolve_auto_codec(
        config.encoder.codec, config.output.container);

    if (codec == VideoCodec::Uncompressed ||
        config.output.container == VideoContainer::Raw) {
        return std::make_unique<RawVideoSink>();
    }

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    return std::make_unique<NativeAvSink>();
#else
    // Compressed encoding is unavailable in a build that did not opt into
    // native FFmpeg. Callers receive no sink rather than silently crossing a
    // process boundary to a second codec/mux authority.
    return {};
#endif
}

} // anonymous namespace

std::unique_ptr<VideoSink> create_video_sink(const VideoSinkConfig& config) {
    return create_builtin_sink(config);
}

} // namespace chronon3d::media::video
