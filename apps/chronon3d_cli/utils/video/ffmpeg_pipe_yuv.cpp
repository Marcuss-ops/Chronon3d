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

// BT.709 YUV encoding with proper BT.709 coefficients:
// Y = 16  + 219 * (0.2126 * r + 0.7152 * g + 0.0722 * b)
// U = 128 + 224 * (-0.1146 * r - 0.3854 * g + 0.5000 * b)
// V = 128 + 224 * (0.5000 * r - 0.4542 * g - 0.0458 * b)
bool FfmpegPipeEncoder::convert_framebuffer_to_yuv420p(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const int w = options_.width;
    const int h = options_.height;

    if (w % 2 != 0 || h % 2 != 0) {
        return false;
    }

    // 1. Convert Linear floats to gamma-corrected sRGB8 in rgba_buffer_
    if (!convert_framebuffer_to_rgba(fb)) {
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

    const uint8_t* rgba = rgba_buffer_.data();

    // 2. Perform parallel conversion from RGBA8 to YUV420P
    // Y plane
    tbb::parallel_for(tbb::blocked_range<int>(0, h, 32), [&](const tbb::blocked_range<int>& r) {
        for (int y = r.begin(); y < r.end(); ++y) {
            uint8_t* dst_y = y_ptr + static_cast<size_t>(y) * w;
            const uint8_t* src_row = rgba + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                const float r = src_row[x * 4 + 0] / 255.0f;
                const float g = src_row[x * 4 + 1] / 255.0f;
                const float b = src_row[x * 4 + 2] / 255.0f;
                const float y_val = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                dst_y[x] = static_cast<uint8_t>(std::clamp(y_val, 0.0f, 1.0f) * 219.0f + 16.5f);
            }
        }
    });

    // UV planes (subsampled 2x2)
    const int uv_width = w / 2;
    const int uv_height = h / 2;
    tbb::parallel_for(tbb::blocked_range<int>(0, uv_height, 16), [&](const tbb::blocked_range<int>& r) {
        for (int uy = r.begin(); uy < r.end(); ++uy) {
            const int y0 = uy * 2;
            const int y1 = y0 + 1;
            const uint8_t* row0 = rgba + static_cast<size_t>(y0) * w * 4;
            const uint8_t* row1 = rgba + static_cast<size_t>(y1) * w * 4;
            uint8_t* dst_u = u_ptr + static_cast<size_t>(uy) * uv_width;
            uint8_t* dst_v = v_ptr + static_cast<size_t>(uy) * uv_width;

            for (int ux = 0; ux < uv_width; ++ux) {
                const int x0 = ux * 2;
                const int x1 = x0 + 1;

                // Average 2x2 pixels
                const float r = 0.25f * (
                    row0[x0 * 4 + 0] + row0[x1 * 4 + 0] +
                    row1[x0 * 4 + 0] + row1[x1 * 4 + 0]) / 255.0f;
                const float g = 0.25f * (
                    row0[x0 * 4 + 1] + row0[x1 * 4 + 1] +
                    row1[x0 * 4 + 1] + row1[x1 * 4 + 1]) / 255.0f;
                const float b = 0.25f * (
                    row0[x0 * 4 + 2] + row0[x1 * 4 + 2] +
                    row1[x0 * 4 + 2] + row1[x1 * 4 + 2]) / 255.0f;

                const float u = -0.1146f * r - 0.3854f * g + 0.5000f * b;
                const float v =  0.5000f * r - 0.4542f * g - 0.0458f * b;

                dst_u[ux] = static_cast<uint8_t>(std::clamp(u, -0.5f, 0.5f) * 224.0f + 128.5f);
                dst_v[ux] = static_cast<uint8_t>(std::clamp(v, -0.5f, 0.5f) * 224.0f + 128.5f);
            }
        }
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

    // 1. Convert Linear floats to gamma-corrected sRGB8 in rgba_buffer_
    if (!convert_framebuffer_to_rgba(fb)) {
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

    const uint8_t* rgba = rgba_buffer_.data();

    // 2. Perform parallel conversion from RGBA8 to NV12
    // Y plane
    tbb::parallel_for(tbb::blocked_range<int>(0, h, 32), [&](const tbb::blocked_range<int>& r) {
        for (int y = r.begin(); y < r.end(); ++y) {
            uint8_t* dst_y = y_ptr + static_cast<size_t>(y) * w;
            const uint8_t* src_row = rgba + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                const float r = src_row[x * 4 + 0] / 255.0f;
                const float g = src_row[x * 4 + 1] / 255.0f;
                const float b = src_row[x * 4 + 2] / 255.0f;
                const float y_val = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                dst_y[x] = static_cast<uint8_t>(std::clamp(y_val, 0.0f, 1.0f) * 219.0f + 16.5f);
            }
        }
    });

    // UV interleaved plane (subsampled 2x2)
    const int uv_width = w / 2;
    const int uv_height = h / 2;
    tbb::parallel_for(tbb::blocked_range<int>(0, uv_height, 16), [&](const tbb::blocked_range<int>& r) {
        for (int uy = r.begin(); uy < r.end(); ++uy) {
            const int y0 = uy * 2;
            const int y1 = y0 + 1;
            const uint8_t* row0 = rgba + static_cast<size_t>(y0) * w * 4;
            const uint8_t* row1 = rgba + static_cast<size_t>(y1) * w * 4;
            uint8_t* dst_uv = uv_ptr + static_cast<size_t>(uy) * uv_width * 2;

            for (int ux = 0; ux < uv_width; ++ux) {
                const int x0 = ux * 2;
                const int x1 = x0 + 1;

                // Average 2x2 pixels
                const float r = 0.25f * (
                    row0[x0 * 4 + 0] + row0[x1 * 4 + 0] +
                    row1[x0 * 4 + 0] + row1[x1 * 4 + 0]) / 255.0f;
                const float g = 0.25f * (
                    row0[x0 * 4 + 1] + row0[x1 * 4 + 1] +
                    row1[x0 * 4 + 1] + row1[x1 * 4 + 1]) / 255.0f;
                const float b = 0.25f * (
                    row0[x0 * 4 + 2] + row0[x1 * 4 + 2] +
                    row1[x0 * 4 + 2] + row1[x1 * 4 + 2]) / 255.0f;

                const float u = -0.1146f * r - 0.3854f * g + 0.5000f * b;
                const float v =  0.5000f * r - 0.4542f * g - 0.0458f * b;

                dst_uv[ux * 2 + 0] = static_cast<uint8_t>(std::clamp(u, -0.5f, 0.5f) * 224.0f + 128.5f);
                dst_uv[ux * 2 + 1] = static_cast<uint8_t>(std::clamp(v, -0.5f, 0.5f) * 224.0f + 128.5f);
            }
        }
    });

    return true;
}

} // namespace chronon3d::cli
