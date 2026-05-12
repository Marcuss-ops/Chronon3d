#include <chronon3d/renderer/video_frame_provider.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cmath>
#include <filesystem>

namespace chronon3d {

namespace {

// Make a stable cache key from path + timestamp.
std::string make_cache_key(const std::string& path, float t) {
    return fmt::format("{}__{:.4f}", path, t);
}

// Shell-escape a path (Linux/macOS).
std::string shell_escape(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

} // namespace

float VideoFrameProvider::source_time(const VideoDesc& v,
                                      Frame comp_frame,
                                      float comp_fps,
                                      float video_duration_seconds) {
    if (comp_fps <= 0.0f) return v.trim_start;
    float t = v.trim_start + (static_cast<float>(comp_frame) / comp_fps) * v.playback_rate;
    if (v.loop && video_duration_seconds > 0.0f)
        t = std::fmod(t, video_duration_seconds);
    return t;
}

bool VideoFrameProvider::ffmpeg_available() {
#ifdef _WIN32
    return std::system("ffmpeg -version >nul 2>nul") == 0;
#else
    return std::system("ffmpeg -version >/dev/null 2>/dev/null") == 0;
#endif
}

std::string VideoFrameProvider::frame_path(const VideoFrameRequest& req) {
    const std::string key = make_cache_key(req.video_path.string(), req.source_time_seconds);
    if (auto it = m_path_cache.find(key); it != m_path_cache.end())
        return it->second;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(req.cache_dir, ec);
    if (ec) {
        spdlog::warn("VideoFrameProvider: cannot create cache dir {}: {}", req.cache_dir.string(), ec.message());
        return "";
    }

    // Output PNG path — use timestamp as part of filename.
    const std::string stem   = req.video_path.stem().string();
    const std::string outfile = (req.cache_dir / fmt::format("{}__{:08.3f}.png", stem, req.source_time_seconds)).string();

    if (fs::exists(outfile)) {
        m_path_cache[key] = outfile;
        return outfile;
    }

    if (!ffmpeg_available()) {
        spdlog::warn("VideoFrameProvider: ffmpeg not found — video layer skipped");
        return "";
    }

    const std::string cmd = fmt::format(
        "ffmpeg -y -ss {:.4f} -i {} -frames:v 1 {} >/dev/null 2>/dev/null",
        req.source_time_seconds,
        shell_escape(req.video_path.string()),
        shell_escape(outfile));

    if (std::system(cmd.c_str()) != 0) {
        spdlog::warn("VideoFrameProvider: ffmpeg failed for {}", req.video_path.string());
        return "";
    }

    m_path_cache[key] = outfile;
    return outfile;
}

std::string VideoFrameProvider::frame_path(const VideoDesc& v,
                                            Frame comp_frame,
                                            float comp_fps,
                                            float video_duration_seconds) {
    VideoFrameRequest req;
    req.video_path           = v.path;
    req.source_time_seconds  = source_time(v, comp_frame, comp_fps, video_duration_seconds);
    req.cache_dir            = m_cache_dir;
    return frame_path(req);
}

} // namespace chronon3d
