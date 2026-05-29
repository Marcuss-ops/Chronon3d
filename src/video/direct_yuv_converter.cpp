#include <chronon3d/video/direct_yuv_converter.hpp>
#include <chronon3d/video/direct_yuv_lut.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace chronon3d::video {

// ============================================================================
//  sRGB gamma LUT — 8-bit linear→sRGB lookup table.
//  Defined at namespace scope (NOT anonymous) so the HWY SIMD variant in
//  direct_yuv_converter_hwy.cpp can link against it via the extern
//  declaration in direct_yuv_lut.hpp.
// ============================================================================

alignas(64) uint8_t g_srgb_lut[65536];
bool g_srgb_lut_ready = false;

namespace {
struct SrgbLutInit {
    SrgbLutInit() {
        for (int i = 0; i < 65536; ++i) {
            const float v = static_cast<float>(i) / 65535.0f;
            g_srgb_lut[i] = Color::linear_to_srgb8(v);
        }
        g_srgb_lut_ready = true;
    }
};
static SrgbLutInit s_lut_init;
} // anonymous namespace

// Coefficients and helpers (YuvCoeffs, get_coeffs, YuvPixel, rgb8_to_yuv)
// are defined in the shared header direct_yuv_lut.hpp.

// ============================================================================
//  YUV420P converter — parallel over 2-row blocks via tbb::parallel_for
// ============================================================================

static DirectYuvResult convert_to_yuv420p_parallel(const DirectYuvRequest& req) {
    const uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();

    const int w = req.width;
    const int h = req.height;
    if (w % 2 != 0 || h % 2 != 0)
        return DirectYuvResult{};
    if (!req.dst_y || !req.dst_u || !req.dst_v)
        return DirectYuvResult{};

    const Framebuffer& src = req.src;
    const int src_stride = src.allocated_width();
    const Color* src_data = src.data();
    const auto& coeffs = get_coeffs(req.color_matrix);

    const int stride_y = req.dst_stride_y ? req.dst_stride_y : w;
    const int stride_u = req.dst_stride_u ? req.dst_stride_u : (w / 2);
    const int stride_v = req.dst_stride_v ? req.dst_stride_v : (w / 2);
    const bool apply_gamma = req.apply_gamma;

    // Each iteration processes a 2-row block: [y_block*2 .. y_block*2+1]
    const int num_blocks = h / 2;

    tbb::parallel_for(tbb::blocked_range<int>(0, num_blocks), [&](const tbb::blocked_range<int>& r) {
        for (int block = r.begin(); block < r.end(); ++block) {
            const int y = block * 2;

            const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
            const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

            uint8_t* y0 = req.dst_y + static_cast<size_t>(y) * stride_y;
            uint8_t* y1 = req.dst_y + static_cast<size_t>(y + 1) * stride_y;
            uint8_t* u_row = req.dst_u + static_cast<size_t>(block) * stride_u;
            uint8_t* v_row = req.dst_v + static_cast<size_t>(block) * stride_v;

            for (int x = 0; x < w; x += 2) {
                const Color& c00 = src_row0[x];
                const Color& c01 = src_row0[x + 1];
                const Color& c10 = src_row1[x];
                const Color& c11 = src_row1[x + 1];

                uint8_t r00, g00, b00;
                uint8_t r01, g01, b01;
                uint8_t r10, g10, b10;
                uint8_t r11, g11, b11;

                if (apply_gamma) {
                    r00 = linear_to_srgb8_fast(c00.r); g00 = linear_to_srgb8_fast(c00.g); b00 = linear_to_srgb8_fast(c00.b);
                    r01 = linear_to_srgb8_fast(c01.r); g01 = linear_to_srgb8_fast(c01.g); b01 = linear_to_srgb8_fast(c01.b);
                    r10 = linear_to_srgb8_fast(c10.r); g10 = linear_to_srgb8_fast(c10.g); b10 = linear_to_srgb8_fast(c10.b);
                    r11 = linear_to_srgb8_fast(c11.r); g11 = linear_to_srgb8_fast(c11.g); b11 = linear_to_srgb8_fast(c11.b);
                } else {
                    auto to8 = [](float v) -> uint8_t {
                        return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                    };
                    r00 = to8(c00.r); g00 = to8(c00.g); b00 = to8(c00.b);
                    r01 = to8(c01.r); g01 = to8(c01.g); b01 = to8(c01.b);
                    r10 = to8(c10.r); g10 = to8(c10.g); b10 = to8(c10.b);
                    r11 = to8(c11.r); g11 = to8(c11.g); b11 = to8(c11.b);
                }

                YuvPixel yuv00 = rgb8_to_yuv(r00, g00, b00, coeffs);
                YuvPixel yuv01 = rgb8_to_yuv(r01, g01, b01, coeffs);
                YuvPixel yuv10 = rgb8_to_yuv(r10, g10, b10, coeffs);
                YuvPixel yuv11 = rgb8_to_yuv(r11, g11, b11, coeffs);

                y0[x]     = yuv00.y;
                y0[x + 1] = yuv01.y;
                y1[x]     = yuv10.y;
                y1[x + 1] = yuv11.y;

                const int ux = x / 2;
                u_row[ux] = static_cast<uint8_t>(
                    (static_cast<unsigned>(yuv00.u) + yuv01.u + yuv10.u + yuv11.u + 2) / 4);
                v_row[ux] = static_cast<uint8_t>(
                    (static_cast<unsigned>(yuv00.v) + yuv01.v + yuv10.v + yuv11.v + 2) / 4);
            }
        }
    });

    const uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    return DirectYuvResult{
        .success       = true,
        .used_simd     = false,
        .conversion_ns = t1 - t0,
    };
}

// ============================================================================
//  NV12 converter — parallel over 2-row blocks via tbb::parallel_for
// ============================================================================

static DirectYuvResult convert_to_nv12_parallel(const DirectYuvRequest& req) {
    const uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();

    const int w = req.width;
    const int h = req.height;
    if (w % 2 != 0 || h % 2 != 0)
        return DirectYuvResult{};
    if (!req.dst_y || !req.dst_uv)
        return DirectYuvResult{};

    const Framebuffer& src = req.src;
    const int src_stride = src.allocated_width();
    const Color* src_data = src.data();
    const auto& coeffs = get_coeffs(req.color_matrix);

    const int stride_y  = req.dst_stride_y  ? req.dst_stride_y  : w;
    const int stride_uv = req.dst_stride_uv ? req.dst_stride_uv : w;
    const bool apply_gamma = req.apply_gamma;

    const int num_blocks = h / 2;

    tbb::parallel_for(tbb::blocked_range<int>(0, num_blocks), [&](const tbb::blocked_range<int>& r) {
        for (int block = r.begin(); block < r.end(); ++block) {
            const int y = block * 2;

            const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
            const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

            uint8_t* y0 = req.dst_y + static_cast<size_t>(y) * stride_y;
            uint8_t* y1 = req.dst_y + static_cast<size_t>(y + 1) * stride_y;
            uint8_t* uv_row = req.dst_uv + static_cast<size_t>(block) * stride_uv;

            for (int x = 0; x < w; x += 2) {
                const Color& c00 = src_row0[x];
                const Color& c01 = src_row0[x + 1];
                const Color& c10 = src_row1[x];
                const Color& c11 = src_row1[x + 1];

                auto to8 = [&](float v) -> uint8_t {
                    return apply_gamma
                        ? linear_to_srgb8_fast(v)
                        : static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                };

                uint8_t r00 = to8(c00.r), g00 = to8(c00.g), b00 = to8(c00.b);
                uint8_t r01 = to8(c01.r), g01 = to8(c01.g), b01 = to8(c01.b);
                uint8_t r10 = to8(c10.r), g10 = to8(c10.g), b10 = to8(c10.b);
                uint8_t r11 = to8(c11.r), g11 = to8(c11.g), b11 = to8(c11.b);

                YuvPixel yuv00 = rgb8_to_yuv(r00, g00, b00, coeffs);
                YuvPixel yuv01 = rgb8_to_yuv(r01, g01, b01, coeffs);
                YuvPixel yuv10 = rgb8_to_yuv(r10, g10, b10, coeffs);
                YuvPixel yuv11 = rgb8_to_yuv(r11, g11, b11, coeffs);

                y0[x]     = yuv00.y;
                y0[x + 1] = yuv01.y;
                y1[x]     = yuv10.y;
                y1[x + 1] = yuv11.y;

                // NV12: interleaved UV pair at each chroma position
                const int uvx = x;
                uv_row[uvx]     = static_cast<uint8_t>(
                    (static_cast<unsigned>(yuv00.u) + yuv01.u + yuv10.u + yuv11.u + 2) / 4);
                uv_row[uvx + 1] = static_cast<uint8_t>(
                    (static_cast<unsigned>(yuv00.v) + yuv01.v + yuv10.v + yuv11.v + 2) / 4);
            }
        }
    });

    const uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    return DirectYuvResult{
        .success       = true,
        .used_simd     = false,
        .conversion_ns = t1 - t0,
    };
}

// ============================================================================
//  Forward declarations for HWY SIMD variants (defined in
//  direct_yuv_converter_hwy.cpp).  These are compiled as a separate
//  translation unit with Highway's multi-target dispatch.
// ============================================================================

DirectYuvResult convert_to_yuv420p_hwy(const DirectYuvRequest& req);
DirectYuvResult convert_to_nv12_hwy(const DirectYuvRequest& req);

// ============================================================================
//  Public API — dispatch: try HWY SIMD first, fall back to scalar TBB
// ============================================================================

DirectYuvResult convert_framebuffer_to_yuv_direct(const DirectYuvRequest& req) {
    if (req.width <= 0 || req.height <= 0) return DirectYuvResult{};
    if (req.width % 2 != 0 || req.height % 2 != 0) return DirectYuvResult{};

    switch (req.format) {
        case EncoderPixelFormat::YUV420P: {
            auto r = convert_to_yuv420p_hwy(req);
            if (r.success) return r;
            return convert_to_yuv420p_parallel(req);
        }
        case EncoderPixelFormat::NV12: {
            auto r = convert_to_nv12_hwy(req);
            if (r.success) return r;
            return convert_to_nv12_parallel(req);
        }
        default:
            return DirectYuvResult{};
    }
}

} // namespace chronon3d::video
