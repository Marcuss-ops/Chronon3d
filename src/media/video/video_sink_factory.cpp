#include <chronon3d/media/video/video_sink_factory.hpp>

#include "raw_video_sink.hpp"
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "encode/native_av_sink.hpp"
#else
#include "ffmpeg_pipe_sink.hpp"
#endif

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace chronon3d::media::video {

namespace {

/// Registered custom factory functions + reader-writer mutex.
///
/// create_video_sink() takes a shared_lock (multiple concurrent reads)
/// and copies the callable BEFORE releasing the lock, then invokes it
/// AFTER the lock is released — so the factory code may safely call
/// register/unregister without deadlock.
///
/// register/unregister take a unique_lock (exclusive write).
struct SinkFactoryState {
    std::unordered_map<std::string, VideoSinkFactoryFn> factories;
    mutable std::shared_mutex mutex;
};

SinkFactoryState& custom_factory_state() {
    static SinkFactoryState state;
    return state;
}

/// Select a built-in sink implementation based on config.
[[nodiscard]] std::unique_ptr<VideoSink> create_builtin_sink(
    const VideoSinkConfig& config)
{
    const auto codec = resolve_auto_codec(
        config.encoder.codec, config.output.container);

    // Raw/uncompressed → RawVideoSink (write raw pixel data to file).
    if (codec == VideoCodec::Uncompressed ||
        config.output.container == VideoContainer::Raw) {
        return std::make_unique<RawVideoSink>();
    }

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    // Production compressed output is always in-process when the native
    // FFmpeg backend is available. libavcodec owns codec mechanics and the
    // sink delegates all packet/container work to MuxSession/libavformat.
    return std::make_unique<NativeAvSink>();
#else
    // Build-time compatibility fallback only. This path is intentionally not
    // selected in native-FFmpeg builds, so a subprocess can never silently
    // become the production canonical encoder.
    return std::make_unique<FfmpegPipeSink>();
#endif
}

} // anonymous namespace

std::unique_ptr<VideoSink> create_video_sink(const VideoSinkConfig& config) {
    const auto path_str = config.output.output_path.string();
    const auto scheme_end = path_str.find("://");
    if (scheme_end != std::string::npos) {
        const auto scheme = path_str.substr(0, scheme_end);

        VideoSinkFactoryFn factory;
        {
            auto& state = custom_factory_state();
            std::shared_lock lock(state.mutex);
            const auto it = state.factories.find(scheme);
            if (it != state.factories.end()) {
                factory = it->second;
            }
        }

        if (factory) {
            return factory(config);
        }
    }

    return create_builtin_sink(config);
}

void register_sink_factory(std::string_view scheme, VideoSinkFactoryFn factory) {
    auto& state = custom_factory_state();
    std::unique_lock lock(state.mutex);
    state.factories[std::string(scheme)] = std::move(factory);
}

void unregister_sink_factory(std::string_view scheme) noexcept {
    auto& state = custom_factory_state();
    std::unique_lock lock(state.mutex);
    state.factories.erase(std::string(scheme));
}

} // namespace chronon3d::media::video
