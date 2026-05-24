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

    if (!dst) {
        const size_t req_size = count * 4u;
        if (rgba_buffer_.size() != req_size) {
            rgba_buffer_.assign(req_size, 0);
        }
    }

    const Color* src = fb.data();
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

        tbb::parallel_for(tbb::blocked_range<size_t>(0, count, 4096),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i < r.end(); ++i) {
                    const Color& c = src[i];
                    dst_ptr[i * 4 + 0] = fast_srgb(c.r);
                    dst_ptr[i * 4 + 1] = fast_srgb(c.g);
                    dst_ptr[i * 4 + 2] = fast_srgb(c.b);
                    dst_ptr[i * 4 + 3] = fast_srgb(c.a);
                }
            });
    } else {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, count, 4096),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i < r.end(); ++i) {
                    const auto rgb = color::linear_to_output_rgb8(src[i], transform);
                    dst_ptr[i * 4 + 0] = rgb.r;
                    dst_ptr[i * 4 + 1] = rgb.g;
                    dst_ptr[i * 4 + 2] = rgb.b;
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

    // Parallel SIMD by row strips
    tbb::parallel_for(tbb::blocked_range<int>(0, h, 32), [&](const tbb::blocked_range<int>& r) {
        simd::convert_f32_rgba_to_nv12_simd_rows(y_ptr, uv_ptr, fb.data(), w, h, r.begin(), r.end());
    });

    return true;
}

} // namespace chronon3d::cli
