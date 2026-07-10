#include <chronon3d/media/video/video_sink_factory.hpp>

#include "raw_video_sink.hpp"
#include "ffmpeg_pipe_sink.hpp"

#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace chronon3d::media::video {

namespace {

/// Registered custom factory functions.
std::unordered_map<std::string, VideoSinkFactoryFn>& custom_factories() {
    static std::unordered_map<std::string, VideoSinkFactoryFn> factories;
    return factories;
}

/// Mutex protecting the custom_factories() map.
/// Reads (create_video_sink) take a shared_lock; writes (register/unregister)
/// take a unique_lock.  Mirrors the pattern in text_rasterizer_cache.cpp.
std::shared_mutex& factories_mutex() {
    static std::shared_mutex mutex;
    return mutex;
}

/// Select a built-in sink implementation based on config.
[[nodiscard]] std::unique_ptr<VideoSink> create_builtin_sink(
    const VideoSinkConfig& config)
{
    const auto codec = config.encoder.codec;

    // Raw/uncompressed → RawVideoSink (write raw pixel data to file).
    if (codec == VideoCodec::Uncompressed ||
        config.output.container == VideoContainer::Raw) {
        return std::make_unique<RawVideoSink>();
    }

    // Compressed codecs → FfmpegPipeSink (pipe to ffmpeg subprocess).
    return std::make_unique<FfmpegPipeSink>();
}

} // anonymous namespace

// ============================================================================
//  create_video_sink()
// ============================================================================

std::unique_ptr<VideoSink> create_video_sink(const VideoSinkConfig& config) {
    // 1. Check custom factories first (scheme-matched on output path).
    const auto path_str = config.output.output_path.string();
    const auto scheme_end = path_str.find("://");
    if (scheme_end != std::string::npos) {
        const auto scheme = path_str.substr(0, scheme_end);
        VideoSinkFactoryFn fn = nullptr;
        {
            std::shared_lock lock(factories_mutex());
            const auto& cf = custom_factories();
            auto it = cf.find(scheme);
            if (it != cf.end()) {
                fn = it->second;
            }
        }
        // Call outside the lock to avoid deadlock if the factory
        // itself calls register_sink_factory() / unregister_sink_factory().
        if (fn) {
            return fn(config);
        }
    }

    // 2. Fall back to built-in sink selection.
    auto sink = create_builtin_sink(config);
    if (sink) {
        return sink;
    }

    // 3. No implementation available.
    return nullptr;
}

// ============================================================================
//  Factory registration
// ============================================================================

void register_sink_factory(std::string_view scheme, VideoSinkFactoryFn factory) {
    std::unique_lock lock(factories_mutex());
    custom_factories()[std::string(scheme)] = factory;
}

void unregister_sink_factory(std::string_view scheme) noexcept {
    std::unique_lock lock(factories_mutex());
    auto& cf = custom_factories();
    cf.erase(std::string(scheme));
}

} // namespace chronon3d::media::video
