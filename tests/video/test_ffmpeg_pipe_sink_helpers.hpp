#pragma once

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/media/video/video_config.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace chronon3d::media::video {
namespace test_ffpipe {

/// Returns true if `ffmpeg` is on PATH.  Cached after first check.
inline bool ffmpeg_available() {
    static int avail = -1;
    if (avail < 0) {
        avail = (std::system("ffmpeg -version > /dev/null 2>&1") == 0) ? 1 : 0;
    }
    return avail == 1;
}

/// Unique temp file path per call.  Thread-safe via atomic counter.
inline std::string temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_ffpipe_" + std::to_string(id) + "_" + suffix;
}

/// Build a minimal valid VideoSinkConfig for the given format/dimensions.
inline VideoSinkConfig make_config(int w, int h, PixelFormat fmt,
                                   const std::string& path,
                                   bool overwrite = true) {
    VideoSinkConfig cfg;
    cfg.stream.width            = w;
    cfg.stream.height           = h;
    cfg.stream.submitted_format = fmt;
    cfg.encoder.encoded_pixel_format =
        (fmt == PixelFormat::RGBA8 || fmt == PixelFormat::RGB24)
            ? PixelFormat::YUV420P
            : fmt;
    cfg.encoder.codec          = VideoCodec::H264;
    cfg.encoder.apply_gamma    = false;
    cfg.encoder.crf            = 30;
    cfg.encoder.preset         = "ultrafast";
    cfg.output.output_path     = std::filesystem::path(path);
    cfg.output.overwrite       = overwrite;
    cfg.output.container       = VideoContainer::Mp4;
    return cfg;
}

/// Fill a buffer with a deterministic byte pattern.
inline void fill_pattern(uint8_t* buf, size_t size, uint8_t seed = 0xAB) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>(seed + i * 7);
    }
}

/// Check that a file exists and has non-zero size.
inline bool file_nonempty(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return !ec && sz > 0;
}

/// RAII file cleanup.
struct TempFile {
    std::string path;
    explicit TempFile(std::string p) : path(std::move(p)) {}
    ~TempFile() noexcept { std::error_code ec; std::filesystem::remove(path, ec); }
};

} // namespace test_ffpipe
} // namespace chronon3d::media::video
