#include "ffmpeg_pipe_encoder.hpp"

#include <chronon3d/math/color.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>

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

std::string pix_fmt_to_ffmpeg_str(PipePixelFormat fmt) {
    switch (fmt) {
        case PipePixelFormat::RGBA:    return "rgba";
        case PipePixelFormat::YUV420P: return "yuv420p";
        case PipePixelFormat::NV12:    return "nv12";
    }
    return "rgba";
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
        pix_fmt_to_ffmpeg_str(options.input_format),
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

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);

    switch (options_.input_format) {
        case PipePixelFormat::RGBA:
            rgba_buffer_.assign(w * h * 4u, 0);
            break;
        case PipePixelFormat::YUV420P:
            y_plane_.assign(w * h, 0);
            u_plane_.assign(w * h / 4u, 0);
            v_plane_.assign(w * h / 4u, 0);
            break;
        case PipePixelFormat::NV12:
            y_plane_.assign(w * h, 0);
            nv12_uv_plane_.assign(w * h / 2u, 0);
            break;
    }

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
        const auto rgb = color::linear_to_output_rgb8(src[i], options_.color_transform);
        dst[i * 4 + 0] = rgb.r;
        dst[i * 4 + 1] = rgb.g;
        dst[i * 4 + 2] = rgb.b;
        // Alpha is not gamma-encoded — store as-is (linear → sRGB is purely for
        // alpha channel visualisation, not a color-space transform).
        dst[i * 4 + 3] = Color::linear_to_srgb8(src[i].a);
    }

    return true;
}

// BT.709 coefficients for HD content
// Y =  0.2126 R + 0.7152 G + 0.0722 B
// U = -0.1146 R - 0.3854 G + 0.5000 B + 128
// V =  0.5000 R - 0.4542 G - 0.0458 B + 128
bool FfmpegPipeEncoder::convert_framebuffer_to_yuv420p(const Framebuffer& fb) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const int w = options_.width;
    const int h = options_.height;

    if (w % 2 != 0 || h % 2 != 0) {
        return false; // YUV420p requires even dimensions
    }

    const size_t y_size  = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t uv_size = y_size / 4u;

    if (y_plane_.size() != y_size || u_plane_.size() != uv_size || v_plane_.size() != uv_size) {
        return false;
    }

    const Color* src = fb.data();

    // Float-level transform avoids uint8 round-trip, preserving precision
    // for the YUV matrix that follows.
    auto srgb_float = [&](const Color& c) -> std::array<float, 3> {
        return color::linear_to_output_rgb_float(c, options_.color_transform);
    };

    // Luma plane (full resolution)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color& c = src[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            const auto srgb = srgb_float(c);
            const float r = srgb[0];
            const float g = srgb[1];
            const float b = srgb[2];

            float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            yy = std::clamp(yy, 0.0f, 1.0f);

            y_plane_[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(yy * 219.0f + 16.0f + 0.5f);
        }
    }

    // Chroma planes (2x2 subsampled)
    for (int y = 0; y < h / 2; ++y) {
        for (int x = 0; x < w / 2; ++x) {
            // Average 2x2 block
            const int base_x = x * 2;
            const int base_y = y * 2;

            float r_sum = 0.0f, g_sum = 0.0f, b_sum = 0.0f;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const Color& c = src[static_cast<size_t>(base_y + dy) * static_cast<size_t>(w) + static_cast<size_t>(base_x + dx)];
                    const auto srgb = srgb_float(c);
                    r_sum += srgb[0];
                    g_sum += srgb[1];
                    b_sum += srgb[2];
                }
            }

            const float r = r_sum / 4.0f;
            const float g = g_sum / 4.0f;
            const float b = b_sum / 4.0f;

            float uu = -0.1146f * r - 0.3854f * g + 0.5000f * b;
            uu = std::clamp(uu, -0.5f, 0.5f);
            u_plane_[static_cast<size_t>(y) * static_cast<size_t>(w / 2) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(uu * 224.0f + 128.0f + 0.5f);

            float vv = 0.5000f * r - 0.4542f * g - 0.0458f * b;
            vv = std::clamp(vv, -0.5f, 0.5f);
            v_plane_[static_cast<size_t>(y) * static_cast<size_t>(w / 2) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(vv * 224.0f + 128.0f + 0.5f);
        }
    }

    return true;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_nv12(const Framebuffer& fb) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    if (options_.width % 2 != 0 || options_.height % 2 != 0) {
        return false;
    }

    // First convert to YUV420p planes, then interleave UV for NV12
    if (!convert_framebuffer_to_yuv420p(fb)) {
        return false;
    }

    const size_t w2 = static_cast<size_t>(options_.width / 2);
    const size_t h2 = static_cast<size_t>(options_.height / 2);
    const size_t uv_total = w2 * h2;

    if (nv12_uv_plane_.size() != uv_total * 2u) {
        return false;
    }

    // Interleave U and V: UVUVUV...
    for (size_t i = 0; i < uv_total; ++i) {
        nv12_uv_plane_[i * 2 + 0] = u_plane_[i];
        nv12_uv_plane_[i * 2 + 1] = v_plane_[i];
    }

    return true;
}

bool FfmpegPipeEncoder::write_frame(const Framebuffer& fb) {
    if (!pipe_) {
        return false;
    }

    auto timed_fwrite = [&](const void* ptr, size_t size, size_t count, FILE* stream) -> size_t {
        const auto t0 = std::chrono::high_resolution_clock::now();
        size_t res = std::fwrite(ptr, size, count, stream);
        const auto t1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
        return res;
    };

    switch (options_.input_format) {
        case PipePixelFormat::RGBA: {
            if (!convert_framebuffer_to_rgba(fb)) {
                return false;
            }
            const size_t written = timed_fwrite(
                rgba_buffer_.data(), 1, rgba_buffer_.size(), pipe_);
            if (written != rgba_buffer_.size()) {
                return false;
            }
            bytes_written_ += written;
            break;
        }
        case PipePixelFormat::YUV420P: {
            if (!convert_framebuffer_to_yuv420p(fb)) {
                return false;
            }
            // Write planes: Y, U, V
            size_t w1 = timed_fwrite(y_plane_.data(), 1, y_plane_.size(), pipe_);
            size_t w2 = timed_fwrite(u_plane_.data(), 1, u_plane_.size(), pipe_);
            size_t w3 = timed_fwrite(v_plane_.data(), 1, v_plane_.size(), pipe_);
            if (w1 != y_plane_.size() || w2 != u_plane_.size() || w3 != v_plane_.size()) {
                return false;
            }
            bytes_written_ += w1 + w2 + w3;
            break;
        }
        case PipePixelFormat::NV12: {
            if (!convert_framebuffer_to_nv12(fb)) {
                return false;
            }
            // Write planes: Y, interleaved UV
            size_t w1 = timed_fwrite(y_plane_.data(), 1, y_plane_.size(), pipe_);
            size_t w2 = timed_fwrite(nv12_uv_plane_.data(), 1, nv12_uv_plane_.size(), pipe_);
            if (w1 != y_plane_.size() || w2 != nv12_uv_plane_.size()) {
                return false;
            }
            bytes_written_ += w1 + w2;
            break;
        }
    }

    ++frames_written_;
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
