#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <array>
#include <tbb/parallel_for.h>

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

    const auto& transform = options_.color_transform;

    if (transform.apply_gamma && transform.output == color::ColorSpace::SRGB) {
        // Use SIMD kernel for fast linear-to-sRGB-ish conversion
        simd::convert_f32_rgba_to_u8_rgba(dst_ptr, src, static_cast<int>(count));
    } else {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, count, 4096),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i < r.end(); ++i) {
                    const auto rgb = color::linear_to_output_rgb8(src[i], transform);
                    dst_ptr[i * 4 + 0] = rgb.r;
                    dst_ptr[i * 4 + 1] = rgb.g;
                    dst_ptr[i * 4 + 2] = rgb.b;
                    // Alpha is not gamma-encoded — store as-is
                    dst_ptr[i * 4 + 3] = Color::linear_to_srgb8(src[i].a);
                }
            });
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
        return false;
    }

    const size_t y_size  = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t uv_size = y_size / 4u;

    uint8_t* y_ptr = dst ? dst : y_plane_.data();
    uint8_t* u_ptr = dst ? (dst + y_size) : u_plane_.data();
    uint8_t* v_ptr = dst ? (dst + y_size + uv_size) : v_plane_.data();

    // Parallel SIMD by row strips
    tbb::parallel_for(tbb::blocked_range<int>(0, h, 32), [&](const tbb::blocked_range<int>& r) {
        simd::convert_f32_rgba_to_yuv420p_simd_rows(y_ptr, u_ptr, v_ptr, fb.data(), w, h, r.begin(), r.end());
    });
    
    return true;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_nv12(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const int w = options_.width;
    const int h = options_.height;

    if (w % 2 != 0 || h % 2 != 0) {
        return false;
    }

    const size_t y_size = static_cast<size_t>(w) * h;
    uint8_t* y_ptr = dst ? dst : y_plane_.data();
    uint8_t* uv_ptr = dst ? (dst + y_size) : nv12_uv_plane_.data();

    // Parallel SIMD by row strips
    tbb::parallel_for(tbb::blocked_range<int>(0, h, 32), [&](const tbb::blocked_range<int>& r) {
        simd::convert_f32_rgba_to_nv12_simd_rows(y_ptr, uv_ptr, fb.data(), w, h, r.begin(), r.end());
    });

    return true;
}

} // namespace chronon3d::cli
