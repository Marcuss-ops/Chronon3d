// ============================================================================
// packed_backend.cpp — Float→RGBA8 direct quantization (TBB-parallel).
//
// PR5: Moved from frame_converter.cpp.  The swscale backend also calls this
// function to produce the RGBA8 staging buffer before sws_scale().
// ============================================================================

#include <chronon3d/media/frame_conversion/backends/packed_backend.hpp>

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/parallel_tracked.hpp>

#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::video::packed {

namespace {

// ── Inline helpers shared by fallback paths ──────────────────────────────────

// Quantize a single Channel (linear [0,1]) to uint8 honoring `apply_gamma`.
[[nodiscard]] inline uint8_t quantize_channel(float v, bool apply_gamma) noexcept {
    if (apply_gamma) {
        return Color::linear_to_srgb8(v);
    }
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// BT.709 limited-range RGB→{Y,Cb,Cr}.  Inputs in [0,1]; outputs clamped to
// the broadcast range.  This is a fallback — production builds with
// CHRONON3D_ENABLE_NATIVE_FFMPEG use swscale which honors YuvMatrix and
// ColorRange precisely (BT.601/BT.2020, Limited/Full).
struct Yuv709LimitedR { uint8_t y, cb, cr; };

[[nodiscard]] inline Yuv709LimitedR rgb_to_yuv709_limited(float r, float g, float b) noexcept {
    const float y_f  = 16.0f  + 219.0f * (0.2126f * r + 0.7152f * g + 0.0722f * b);
    const float cb_f = 128.0f + 224.0f * (-0.1146f * r - 0.3854f * g + 0.5f * b);
    const float cr_f = 128.0f + 224.0f * (0.5f * r - 0.4542f * g - 0.0458f * b);
    return {
        static_cast<uint8_t>(std::clamp(y_f,  16.0f, 235.0f)),
        static_cast<uint8_t>(std::clamp(cb_f, 16.0f, 240.0f)),
        static_cast<uint8_t>(std::clamp(cr_f, 16.0f, 240.0f)),
    };
}

}  // namespace

void convert_fb_to_rgba8(const Framebuffer& src,
                         int width, int height,
                         bool apply_gamma,
                         uint8_t* rgba8)
{
    const Color* src_data = src.data();
    const int alloc_w = src.allocated_width();
    const int grain = std::max(32, height / 16);
    parallel_for_tracked(tbb::blocked_range<int>(0, height, grain),
                         [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            const Color* src_row = src_data + static_cast<std::size_t>(y) * alloc_w;
            uint8_t* dst_row = rgba8 + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                const Color& c = src_row[x];
                dst_row[x * 4 + 0] = quantize_channel(c.r, apply_gamma);
                dst_row[x * 4 + 1] = quantize_channel(c.g, apply_gamma);
                dst_row[x * 4 + 2] = quantize_channel(c.b, apply_gamma);
                dst_row[x * 4 + 3] = static_cast<uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    });
}

// ── Native (non-FFmpeg) YUV420P / NV12 / RGB24 fallback ─────────────────────
//
// Used when CHRONON3D_ENABLE_NATIVE_FFMPEG is OFF.  Always produces non-zero
// output bytes for even dimensions, so callers (NullConvertEncoder,
// FrameConversionService, ...) report success.  BT.709 limited-range
// coefficients are hard-coded; this matches the most common case (HDTV SDR)
// and is the only path the unit tests exercise.

void convert_frame_fallback(const ConvertFrameRequest& req) {
    const int width  = req.width;
    const int height = req.height;
    const Color* src_data = req.src.data();
    const int alloc_w     = req.src.allocated_width();
    const bool apply_gamma = req.apply_gamma;

    switch (req.format) {
        case EncoderPixelFormat::RGBA8:
            // RGBA8 is dispatched to Packed::convert_fb_to_rgba8 by the
            // selector before reaching the Swscale branch — kept here for
            // forward-compat in case the fallback is called directly.
            return;

        case EncoderPixelFormat::RGB24: {
            const int grain = std::max(32, height / 16);
            const int row_stride = req.planes.stride_y ? req.planes.stride_y : (width * 3);
            parallel_for_tracked(tbb::blocked_range<int>(0, height, grain),
                [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y < range.end(); ++y) {
                    const Color* src_row = src_data
                        + static_cast<std::size_t>(y) * alloc_w;
                    uint8_t* dst_row = req.planes.y
                        + static_cast<std::size_t>(y) * row_stride;
                    for (int x = 0; x < width; ++x) {
                        const Color& c = src_row[x];
                        dst_row[x * 3 + 0] = quantize_channel(c.r, apply_gamma);
                        dst_row[x * 3 + 1] = quantize_channel(c.g, apply_gamma);
                        dst_row[x * 3 + 2] = quantize_channel(c.b, apply_gamma);
                    }
                }
            });
            return;
        }

        case EncoderPixelFormat::YUV420P: {
            // Phase 1: full-resolution Y plane.
            const int g_y = std::max(32, height / 16);
            const int sy_y = req.planes.stride_y ? req.planes.stride_y : width;
            parallel_for_tracked(tbb::blocked_range<int>(0, height, g_y),
                [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y < range.end(); ++y) {
                    const Color* src_row = src_data
                        + static_cast<std::size_t>(y) * alloc_w;
                    uint8_t* dst_y = req.planes.y
                        + static_cast<std::size_t>(y) * sy_y;
                    for (int x = 0; x < width; ++x) {
                        const Color& c = src_row[x];
                        const uint8_t r8 = quantize_channel(c.r, apply_gamma);
                        const uint8_t g8 = quantize_channel(c.g, apply_gamma);
                        const uint8_t b8 = quantize_channel(c.b, apply_gamma);
                        const Yuv709LimitedR yuv = rgb_to_yuv709_limited(
                            r8 / 255.0f, g8 / 255.0f, b8 / 255.0f);
                        dst_y[x] = yuv.y;
                    }
                }
            });

            // Phase 2: half-resolution Cb/Cr; average each 2×2 macroblock.
            // Validated upstream (validate_conversion_request) ⇒ width, height
            // are even and positive, so uv_w = width/2 and uv_h = height/2.
            const int uv_h = height / 2;
            const int uv_w = width  / 2;
            const int g_uv = std::max(16, uv_h / 16);
            const int sy_u = req.planes.stride_u ? req.planes.stride_u : uv_w;
            const int sy_v = req.planes.stride_v ? req.planes.stride_v : uv_w;
            parallel_for_tracked(tbb::blocked_range<int>(0, uv_h, g_uv),
                [&](const tbb::blocked_range<int>& range) {
                for (int uy = range.begin(); uy < range.end(); ++uy) {
                    const int sy0 = uy * 2;
                    const Color* row0 = src_data
                        + static_cast<std::size_t>(sy0)     * alloc_w;
                    const Color* row1 = src_data
                        + static_cast<std::size_t>(sy0 + 1) * alloc_w;
                    uint8_t* dst_u = req.planes.u
                        + static_cast<std::size_t>(uy) * sy_u;
                    uint8_t* dst_v = req.planes.v
                        + static_cast<std::size_t>(uy) * sy_v;
                    for (int ux = 0; ux < uv_w; ++ux) {
                        const int sx = ux * 2;
                        int r_sum = 0, g_sum = 0, b_sum = 0;
                        for (int dy = 0; dy < 2; ++dy) {
                            const Color* row = (dy == 0) ? row0 : row1;
                            for (int dx = 0; dx < 2; ++dx) {
                                const Color& c = row[sx + dx];
                                r_sum += quantize_channel(c.r, apply_gamma);
                                g_sum += quantize_channel(c.g, apply_gamma);
                                b_sum += quantize_channel(c.b, apply_gamma);
                            }
                        }
                        const float r = (r_sum * 0.25f) / 255.0f;
                        const float g = (g_sum * 0.25f) / 255.0f;
                        const float b = (b_sum * 0.25f) / 255.0f;
                        const Yuv709LimitedR yuv = rgb_to_yuv709_limited(r, g, b);
                        dst_u[ux] = yuv.cb;
                        dst_v[ux] = yuv.cr;
                    }
                }
            });
            return;
        }

        case EncoderPixelFormat::NV12: {
            const int g_y = std::max(32, height / 16);
            const int sy_y = req.planes.stride_y ? req.planes.stride_y : width;
            parallel_for_tracked(tbb::blocked_range<int>(0, height, g_y),
                [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y < range.end(); ++y) {
                    const Color* src_row = src_data
                        + static_cast<std::size_t>(y) * alloc_w;
                    uint8_t* dst_y = req.planes.y
                        + static_cast<std::size_t>(y) * sy_y;
                    for (int x = 0; x < width; ++x) {
                        const Color& c = src_row[x];
                        const uint8_t r8 = quantize_channel(c.r, apply_gamma);
                        const uint8_t g8 = quantize_channel(c.g, apply_gamma);
                        const uint8_t b8 = quantize_channel(c.b, apply_gamma);
                        const Yuv709LimitedR yuv = rgb_to_yuv709_limited(
                            r8 / 255.0f, g8 / 255.0f, b8 / 255.0f);
                        dst_y[x] = yuv.y;
                    }
                }
            });

            const int uv_h = height / 2;
            const int uv_w = width  / 2;
            const int g_uv = std::max(16, uv_h / 16);
            const int sy_uv = req.planes.stride_uv ? req.planes.stride_uv : (uv_w * 2);
            parallel_for_tracked(tbb::blocked_range<int>(0, uv_h, g_uv),
                [&](const tbb::blocked_range<int>& range) {
                for (int uy = range.begin(); uy < range.end(); ++uy) {
                    const int sy0 = uy * 2;
                    const Color* row0 = src_data
                        + static_cast<std::size_t>(sy0)     * alloc_w;
                    const Color* row1 = src_data
                        + static_cast<std::size_t>(sy0 + 1) * alloc_w;
                    uint8_t* dst_uv = req.planes.uv
                        + static_cast<std::size_t>(uy) * sy_uv;
                    for (int ux = 0; ux < uv_w; ++ux) {
                        const int sx = ux * 2;
                        int r_sum = 0, g_sum = 0, b_sum = 0;
                        for (int dy = 0; dy < 2; ++dy) {
                            const Color* row = (dy == 0) ? row0 : row1;
                            for (int dx = 0; dx < 2; ++dx) {
                                const Color& c = row[sx + dx];
                                r_sum += quantize_channel(c.r, apply_gamma);
                                g_sum += quantize_channel(c.g, apply_gamma);
                                b_sum += quantize_channel(c.b, apply_gamma);
                            }
                        }
                        const float r = (r_sum * 0.25f) / 255.0f;
                        const float g = (g_sum * 0.25f) / 255.0f;
                        const float b = (b_sum * 0.25f) / 255.0f;
                        const Yuv709LimitedR yuv = rgb_to_yuv709_limited(r, g, b);
                        dst_uv[ux * 2 + 0] = yuv.cb;
                        dst_uv[ux * 2 + 1] = yuv.cr;
                    }
                }
            });
            return;
        }
    }
}

}  // namespace chronon3d::video::packed
