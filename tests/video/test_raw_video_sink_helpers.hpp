#pragma once

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/media/video/video_config.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace chronon3d::media::video {
namespace test_raw_sink {

/// Unique temp file path per call.  Thread-safe via atomic counter.
inline std::string temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_raw_sink_" + std::to_string(id) + "_" + suffix + ".raw";
}

/// Build a minimal valid VideoSinkConfig for the given format/dimensions.
inline VideoSinkConfig make_config(int w, int h, PixelFormat fmt,
                                   const std::string& path,
                                   bool overwrite = true) {
    VideoSinkConfig cfg;
    cfg.stream.width  = w;
    cfg.stream.height = h;
    cfg.stream.submitted_format = fmt;
    cfg.encoder.encoded_pixel_format = fmt;
    cfg.encoder.apply_gamma = false;
    cfg.transport.asynchronous = false;
    cfg.output.output_path = std::filesystem::path(path);
    cfg.output.overwrite = overwrite;
    cfg.transport.asynchronous = false;
    return cfg;
}

/// Fill a buffer with a deterministic byte pattern for validation.
inline void fill_pattern(uint8_t* buf, size_t size, uint8_t seed = 0xAB) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>(seed + i * 7);
    }
}

/// Check that a file contains exactly the expected number of bytes.
inline bool file_has_size(const std::string& path, size_t expected) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return !ec && sz == expected;
}

} // namespace test_raw_sink
} // namespace chronon3d::media::video
