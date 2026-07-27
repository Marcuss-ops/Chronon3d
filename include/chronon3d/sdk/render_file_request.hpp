#pragma once

#include <chronon3d/sdk/render_request.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>

namespace chronon3d {
class Composition;
}

namespace chronon3d::sdk {

/// Codec requested for file rendering. Auto selects the container default.
enum class VideoCodec : std::uint8_t {
    Auto = 0,
    H264 = 1,
    H265 = 2,
    VP9  = 3,
    AV1  = 4,
};

/// Container requested for file rendering.
enum class VideoContainer : std::uint8_t {
    Auto = 0,
    Mp4  = 1,
    Mkv  = 2,
    WebM = 3,
};

/// Stable SDK-facing video encoder settings.
struct VideoSettings {
    VideoCodec codec{VideoCodec::Auto};
    VideoContainer container{VideoContainer::Auto};
    std::int64_t bitrate{0};
    int crf{-1};
    bool overwrite{true};
};

/// A contiguous frame range rendered through the canonical render pipeline.
struct RenderFileRequest {
    const chronon3d::Composition* composition{nullptr};
    std::filesystem::path output_path;
    Frame start_frame{0};
    Frame end_frame{0};
    Frame step{1};
    FrameRate frame_rate{30, 1};
    VideoSettings video{};
};

/// Optional cooperative controls for a file render.
struct RenderCallbacks {
    std::function<void(Frame current, Frame total)> progress;
    std::function<bool()> is_cancelled;
};

/// Summary returned after a successful file render.
struct RenderReport {
    std::filesystem::path output_path;
    std::uint64_t rendered_frames{0};
    double elapsed_seconds{0.0};
};

} // namespace chronon3d::sdk
