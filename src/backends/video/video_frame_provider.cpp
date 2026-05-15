#include <chronon3d/backends/video/video_frame_provider.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <fstream>

namespace chronon3d {

float VideoFrameProvider::source_time(const VideoDesc& v, Frame comp_frame, float comp_fps, float video_duration_seconds) {
    float local_time = (float)comp_frame / comp_fps;
    float source_t = v.trim_start + local_time * v.playback_rate;

    if (v.loop && video_duration_seconds > 0.001f) {
        source_t = std::fmod(source_t, video_duration_seconds);
    }
    return source_t;
}

std::string VideoFrameProvider::frame_path(const VideoFrameRequest& request) {
    if (request.video_path.empty()) return "";

    std::error_code ec;
    if (!std::filesystem::exists(m_cache_dir)) {
        std::filesystem::create_directories(m_cache_dir, ec);
    }

    // Generate a unique filename based on video path and time
    auto stem = request.video_path.stem().string();
    auto hash = std::hash<std::string>{}(request.video_path.string());
    
    // round to nearest 1/1000th of a second for filename stability
    long long ms = (long long)std::round(request.source_time_seconds * 1000.0);
    std::string out_name = fmt::format("{}_{:x}_{:08d}.png", stem, hash, ms);
    std::filesystem::path out_path = m_cache_dir / out_name;

    if (std::filesystem::exists(out_path)) {
        return out_path.string();
    }

    // FFmpeg extraction command
    // Use -ss BEFORE -i for fast seeking
    std::string cmd = fmt::format("ffmpeg -y -ss {:.3f} -i \"{}\" -frames:v 1 \"{}\" > NUL 2>&1",
                                  request.source_time_seconds,
                                  request.video_path.string(),
                                  out_path.string());

    int res = std::system(cmd.c_str());
    if (res != 0 || !std::filesystem::exists(out_path)) {
        spdlog::debug("VideoFrameProvider: ffmpeg failed for {} at {}s", request.video_path.string(), request.source_time_seconds);
        return "";
    }

    return out_path.string();
}

std::string VideoFrameProvider::frame_path(const VideoDesc& v, Frame comp_frame, float comp_fps, float video_duration_seconds) {
    VideoFrameRequest req;
    req.video_path = v.path;
    req.source_time_seconds = source_time(v, comp_frame, comp_fps, video_duration_seconds);
    req.cache_dir = m_cache_dir;
    return frame_path(req);
}

bool VideoFrameProvider::ffmpeg_available() {
    static std::optional<bool> available;
    if (available.has_value()) return *available;

    // Try running ffmpeg -version
    int res = std::system("ffmpeg -version > NUL 2>&1");
    available = (res == 0);
    return *available;
}

} // namespace chronon3d
