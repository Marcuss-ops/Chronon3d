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

    if (!dst) {
        const size_t req_size =
            static_cast<size_t>(options_.width) *
            static_cast<size_t>(options_.height) * 4u;
        if (rgba_buffer_.size() != req_size) {
            rgba_buffer_.assign(req_size, 0);
        }
    }

    uint8_t* dst_ptr = dst ? dst : rgba_buffer_.data();
    const auto& transform = options_.color_transform;
    const bool clamp = transform.clamp;
    const bool apply_gamma = transform.apply_gamma;
    const auto output_space = transform.output;

    if (apply_gamma && (output_space == color::ColorSpace::SRGB || output_space == color::ColorSpace::Rec709)) {
        const auto& lut = color_detail::linear_to_srgb8_lut();
        const uint8_t* lut_ptr = lut.data();
        const size_t lut_size_minus_1 = lut.size() - 1;

        auto fast_srgb = [lut_ptr, lut_size_minus_1](float val) -> uint8_t {
            const float clamped = (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);
            const size_t idx = static_cast<size_t>(clamped * static_cast<float>(lut_size_minus_1) + 0.5f);
            return lut_ptr[idx];
        };

        tbb::parallel_for(0, options_.height, [&](int y) {
            const Color* src_row = fb.pixels_row(y);
            uint8_t* dst_row = dst_ptr + static_cast<size_t>(y) * static_cast<size_t>(options_.width) * 4u;
            for (int x = 0; x < options_.width; ++x) {
                const Color& c = src_row[x];
                dst_row[x * 4 + 0] = fast_srgb(c.r);
                dst_row[x * 4 + 1] = fast_srgb(c.g);
                dst_row[x * 4 + 2] = fast_srgb(c.b);
                dst_row[x * 4 + 3] = fast_srgb(c.a);
            }
        });
    } else {
        tbb::parallel_for(0, options_.height, [&](int y) {
            const Color* src_row = fb.pixels_row(y);
            uint8_t* dst_row = dst_ptr + static_cast<size_t>(y) * static_cast<size_t>(options_.width) * 4u;
            for (int x = 0; x < options_.width; ++x) {
                const auto rgb = color::linear_to_output_rgb8(src_row[x], transform);
                dst_row[x * 4 + 0] = rgb.r;
                dst_row[x * 4 + 1] = rgb.g;
                dst_row[x * 4 + 2] = rgb.b;
                dst_row[x * 4 + 3] = Color::linear_to_srgb8(src_row[x].a);
            }
        });
    }

    return true;
}

// BT.709 YUV encoding with optimized integer fixed-point math:
// Y = 16  + ((2992 * r + 10063 * g + 1016 * b) >> 14)
// U = 128 + ((-1648 * r - 5548 * g + 7196 * b) >> 14)
// V = 128 + ((7196 * r - 6536 * g - 660 * b) >> 14)
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

    if (!dst) {
        const size_t req_size = y_size + uv_size + uv_size;
        if (yuv_buffer_.size() != req_size) {
            yuv_buffer_.assign(req_size, 0);
        }
        dst = yuv_buffer_.data();
    }

    uint8_t* y_ptr = dst;
    uint8_t* u_ptr = dst + y_size;
    uint8_t* v_ptr = dst + y_size + uv_size;
    const bool apply_gamma = options_.color_transform.apply_gamma;

    // Use a coarser grain size so each task amortizes the SIMD setup cost.
    // The previous 2-row granularity created too many tiny tasks for 1080p exports.
    tbb::parallel_for(tbb::blocked_range<int>(0, h / 2, 8), [&](const tbb::blocked_range<int>& range) {
        const int y_start = range.begin() * 2;
        const int y_end = range.end() * 2;
        chronon3d::simd::convert_f32_rgba_to_yuv420p_simd_rows(
            y_ptr, u_ptr, v_ptr, fb.data(), w, h, fb.allocated_width(), y_start, y_end, apply_gamma);
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

    const size_t y_size = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t uv_size = y_size / 2u;

    if (!dst) {
        const size_t req_size = y_size + uv_size;
        if (yuv_buffer_.size() != req_size) {
            yuv_buffer_.assign(req_size, 0);
        }
        dst = yuv_buffer_.data();
    }

    uint8_t* y_ptr = dst;
    uint8_t* uv_ptr = dst + y_size;

    const bool apply_gamma = options_.color_transform.apply_gamma;

    // Same coarse-grain scheduling as YUV420P to cut down task overhead.
    tbb::parallel_for(tbb::blocked_range<int>(0, h / 2, 8), [&](const tbb::blocked_range<int>& range) {
        const int y_start = range.begin() * 2;
        const int y_end = range.end() * 2;
        chronon3d::simd::convert_f32_rgba_to_nv12_simd_rows(
            y_ptr, uv_ptr, fb.data(), w, h, fb.allocated_width(), y_start, y_end, apply_gamma);
    });

    return true;
}

} // namespace chronon3d::cli
