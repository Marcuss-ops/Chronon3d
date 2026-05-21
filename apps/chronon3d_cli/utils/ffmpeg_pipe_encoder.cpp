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
    const std::string log_flags = options.verbose
        ? ""
        : "-hide_banner -loglevel error ";

    return fmt::format(
        "ffmpeg -y "
        "{}"
        "-f rawvideo "
        "-pix_fmt {} "
        "-s {}x{} "
        "-r {} "
        "-i - "
        "-an "
        "-c:v {} "
        "-crf {} "
        "-preset {} "
        "-pix_fmt {} "
        "{}",
        log_flags,
        options.input_pix_fmt,
        options.width,
        options.height,
        options.fps,
        options.codec,
        options.crf,
        options.preset,
        options.output_pix_fmt,
        quote_path(options.output_path)
    );
}

bool FfmpegPipeEncoder::open(const FfmpegPipeOptions& options) {
    if (pipe_) {
        return false;
    }

    if (options.width <= 0 || options.height <= 0 ||
        options.fps <= 0 || options.output_path.empty()) {
        return false;
    }

    options_ = options;
    frames_written_ = 0;
    bytes_written_ = 0;

    const size_t frame_bytes =
        static_cast<size_t>(options_.width) *
        static_cast<size_t>(options_.height) *
        4u;

    rgba_buffer_.assign(frame_bytes, 0);

    const std::string cmd = build_ffmpeg_raw_pipe_command(options_);

#if defined(_WIN32)
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
#endif

    return pipe_ != nullptr;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_rgba(const Framebuffer& fb) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const size_t count =
        static_cast<size_t>(options_.width) *
        static_cast<size_t>(options_.height);

    if (rgba_buffer_.size() != count * 4u) {
        return false;
    }

    const Color* src = fb.data();
    uint8_t* dst = rgba_buffer_.data();

    for (size_t i = 0; i < count; ++i) {
        dst[i * 4 + 0] = Color::linear_to_srgb8(src[i].r);
        dst[i * 4 + 1] = Color::linear_to_srgb8(src[i].g);
        dst[i * 4 + 2] = Color::linear_to_srgb8(src[i].b);
        dst[i * 4 + 3] = Color::linear_to_srgb8(src[i].a);
    }

    return true;
}

bool FfmpegPipeEncoder::write_frame(const Framebuffer& fb) {
    if (!pipe_) {
        return false;
    }

    if (!convert_framebuffer_to_rgba(fb)) {
        return false;
    }

    const size_t written = std::fwrite(
        rgba_buffer_.data(),
        1,
        rgba_buffer_.size(),
        pipe_
    );

    if (written != rgba_buffer_.size()) {
        return false;
    }

    ++frames_written_;
    bytes_written_ += written;

    return true;
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
