#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <array>

namespace chronon3d::cli {

bool FfmpegPipeEncoder::convert_framebuffer_to_rgba(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const size_t count =
        static_cast<size_t>(options_.width) *
        static_cast<size_t>(options_.height);

    if (!dst && rgba_buffer_.size() != count * 4u) {
        return false;
    }

    const Color* src = fb.data();
    uint8_t* dst_ptr = dst ? dst : rgba_buffer_.data();

    for (size_t i = 0; i < count; ++i) {
        const auto rgb = color::linear_to_output_rgb8(src[i], options_.color_transform);
        dst_ptr[i * 4 + 0] = rgb.r;
        dst_ptr[i * 4 + 1] = rgb.g;
        dst_ptr[i * 4 + 2] = rgb.b;
        // Alpha is not gamma-encoded — store as-is
        dst_ptr[i * 4 + 3] = Color::linear_to_srgb8(src[i].a);
    }

    return true;
}

// BT.709 coefficients for HD content
// Y =  0.2126 R + 0.7152 G + 0.0722 B
// U = -0.1146 R - 0.3854 G + 0.5000 B + 128
// V =  0.5000 R - 0.4542 G - 0.0458 B + 128
bool FfmpegPipeEncoder::convert_framebuffer_to_yuv420p(const Framebuffer& fb, uint8_t* dst) {
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

    if (!dst && (y_plane_.size() != y_size || u_plane_.size() != uv_size || v_plane_.size() != uv_size)) {
        return false;
    }

    uint8_t* y_ptr = dst ? dst : y_plane_.data();
    uint8_t* u_ptr = dst ? (dst + y_size) : u_plane_.data();
    uint8_t* v_ptr = dst ? (dst + y_size + uv_size) : v_plane_.data();

    const Color* src = fb.data();

    // Float-level transform avoids uint8 round-trip, preserving precision
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

            y_ptr[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
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
            u_ptr[static_cast<size_t>(y) * static_cast<size_t>(w / 2) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(uu * 224.0f + 128.0f + 0.5f);

            float vv = 0.5000f * r - 0.4542f * g - 0.0458f * b;
            vv = std::clamp(vv, -0.5f, 0.5f);
            v_ptr[static_cast<size_t>(y) * static_cast<size_t>(w / 2) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(vv * 224.0f + 128.0f + 0.5f);
        }
    }

    return true;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_nv12(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    if (options_.width % 2 != 0 || options_.height % 2 != 0) {
        return false;
    }

    // First convert to YUV420p planes
    if (!convert_framebuffer_to_yuv420p(fb, nullptr)) {
        return false;
    }

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);
    const size_t y_size = w * h;
    const size_t uv_total = (w / 2) * (h / 2);

    if (!dst && nv12_uv_plane_.size() != uv_total * 2u) {
        return false;
    }

    uint8_t* y_ptr = dst ? dst : y_plane_.data();
    uint8_t* uv_ptr = dst ? (dst + y_size) : nv12_uv_plane_.data();

    if (dst) {
        std::copy(y_plane_.begin(), y_plane_.end(), y_ptr);
    }

    // Interleave U and V: UVUVUV...
    for (size_t i = 0; i < uv_total; ++i) {
        uv_ptr[i * 2 + 0] = u_plane_[i];
        uv_ptr[i * 2 + 1] = v_plane_[i];
    }

    return true;
}

} // namespace chronon3d::cli
