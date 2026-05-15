#pragma once

// VideoFrameProvider v1 -- extracts frames from a video file via ffmpeg.
// Does NOT link against libavcodec; uses a subprocess call to ffmpeg CLI.
// Extracted frames are cached as PNG files in a configurable temp directory.
//
// Usage:
//   VideoFrameProvider vfp;
//   std::string png = vfp.frame_path("assets/clip.mp4", 1.5f, "output/.vcache");
//   if (!png.empty()) {
//       // load png with stb_image as normal image
//   }

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/time.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace chronon3d {

struct VideoFrameRequest {
    std::filesystem::path video_path;
    float                 source_time_seconds{0.0f};
    std::filesystem::path cache_dir{"output/.vcache"};
};

struct VideoDesc {
    std::string path;           // path to video file
    float       trim_start{0.0f};    // seconds into source
    float       playback_rate{1.0f};
    bool        loop{false};
    Vec2        size{0.0f, 0.0f};    // 0,0 = use native size
    float       opacity{1.0f};
    Vec3        pos{0.0f, 0.0f, 0.0f};
};

// Resolves a video frame to a PNG path.
// Returns empty string if ffmpeg is unavailable or fails.
class VideoFrameProvider {
public:
    explicit VideoFrameProvider(std::filesystem::path cache_dir = "output/.vcache")
        : m_cache_dir(std::move(cache_dir)) {}

    // Compute the source timestamp for composition frame `comp_frame`
    // given the composition fps and the VideoDesc settings.
    [[nodiscard]] static float source_time(const VideoDesc& v,
                                           Frame comp_frame,
                                           float comp_fps,
                                           float video_duration_seconds = 0.0f);

    // Extract one frame (nearest to `request.source_time_seconds`) and return
    // the local path to the cached PNG.  Returns "" on failure.
    [[nodiscard]] std::string frame_path(const VideoFrameRequest& request);

    // Convenience: compute time + extract in one call.
    [[nodiscard]] std::string frame_path(const VideoDesc& v,
                                         Frame comp_frame,
                                         float comp_fps,
                                         float video_duration_seconds = 0.0f);

    // Returns true if ffmpeg is findable on PATH.
    [[nodiscard]] static bool ffmpeg_available();

private:
    std::filesystem::path                        m_cache_dir;
    std::unordered_map<std::string, std::string> m_path_cache;
};

} // namespace chronon3d
