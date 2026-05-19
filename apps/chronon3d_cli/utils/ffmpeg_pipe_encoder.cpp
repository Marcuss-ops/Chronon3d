#include "ffmpeg_pipe_encoder.hpp"

#include <chronon3d/math/color.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
    #include <io.h>
#endif

namespace chronon3d::cli {
namespace {

uint8_t to_u8(f32 value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

std::string quote_path(const std::string& path) {
    std::string out = "\"";
    for (char c : path) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

} // namespace

std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options) {
    return fmt::format(
        "ffmpeg -y "
        "-f rawvideo "
        "-pix_fmt rgba "
        "-s {}x{} "
        "-r {} "
        "-i - "
        "-an "
        "-c:v {} "
        "-crf {} "
        "-preset {} "
        "-pix_fmt yuv420p "
        "{}",
        options.width,
        options.height,
        options.fps,
        options.codec,
        options.crf,
        options.preset,
        quote_path(options.output_path)
    );
}

bool FfmpegPipeEncoder::open(const FfmpegPipeOptions& options) {
    if (pipe_) {
        return false;
    }

    if (options.width <= 0 || options.height <= 0 || options.fps <= 0 || options.output_path.empty()) {
        return false;
    }

    options_ = options;
    const std::string cmd = build_ffmpeg_raw_pipe_command(options_);

#if defined(_WIN32)
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
#endif

    return pipe_ != nullptr;
}

bool FfmpegPipeEncoder::write_frame(const Framebuffer& fb) {
    if (!pipe_) {
        return false;
    }

    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    std::vector<uint8_t> rgba;
    rgba.resize(static_cast<size_t>(options_.width) * static_cast<size_t>(options_.height) * 4);

    size_t i = 0;
    for (int y = 0; y < options_.height; ++y) {
        for (int x = 0; x < options_.width; ++x) {
            const Color c = fb.get_pixel(x, y);
            rgba[i++] = to_u8(c.r);
            rgba[i++] = to_u8(c.g);
            rgba[i++] = to_u8(c.b);
            rgba[i++] = to_u8(c.a);
        }
    }

    const size_t written = std::fwrite(rgba.data(), 1, rgba.size(), pipe_);
    return written == rgba.size();
}

bool FfmpegPipeEncoder::close() {
    if (!pipe_) {
        return true;
    }

#if defined(_WIN32)
    const int rc = _pclose(pipe_);
#else
    const int rc = pclose(pipe_);
#endif

    pipe_ = nullptr;
    return rc == 0;
}

FfmpegPipeEncoder::~FfmpegPipeEncoder() {
    if (pipe_) {
        close();
    }
}

} // namespace chronon3d::cli
