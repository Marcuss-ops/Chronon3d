// ============================================================================
// reference_yuv_converter.cpp — Single-threaded scalar YUV oracle (PR4B).
//
// Uses Color::linear_to_srgb8() instead of the deleted gamma LUT.
// Local types and coefficients — no dependency on deleted headers.
// ============================================================================

#include "reference_yuv_converter.hpp"
#include <chronon3d/math/color.hpp>
#include <algorithm>

namespace chronon3d::video {

// ── Local types (formerly from direct_yuv_lut.hpp) ─────────────────────

struct YuvCoeffs {
    float kr, kg, kb;
    float cb_r, cb_g, cb_b;
    float cr_r, cr_g, cr_b;
};

struct YuvPixel { uint8_t y; uint8_t u; uint8_t v; };

static constexpr YuvCoeffs kRefCoeffsBT709{
    .kr = 0.2126f, .kg = 0.7152f, .kb = 0.0722f,
    .cb_r = -0.1146f, .cb_g = -0.3854f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.4542f, .cr_b = -0.0458f,
};

static constexpr YuvCoeffs kRefCoeffsBT601{
    .kr = 0.299f, .kg = 0.587f, .kb = 0.114f,
    .cb_r = -0.168736f, .cb_g = -0.331264f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.418688f, .cr_b = -0.081312f,
};

static constexpr YuvCoeffs kRefCoeffsBT2020{
    .kr = 0.2627f, .kg = 0.6780f, .kb = 0.0593f,
    .cb_r = -0.13963f, .cb_g = -0.36037f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.45979f, .cr_b = -0.04021f,
};

namespace {

inline const YuvCoeffs& get_coeffs(YuvMatrix matrix) {
    switch (matrix) {
        case YuvMatrix::BT601:  return kRefCoeffsBT601;
        case YuvMatrix::BT2020: return kRefCoeffsBT2020;
        case YuvMatrix::BT709:
        default:                return kRefCoeffsBT709;
    }
}

constexpr YuvPixel rgb8_to_yuv_ref(uint8_t r, uint8_t g, uint8_t b, const YuvCoeffs& c) noexcept {
    const int y_val = static_cast<int>(16.5f
        + 219.0f * (c.kr * r + c.kg * g + c.kb * b) / 255.0f + 0.5f);
    const int u_val = static_cast<int>(128.5f
        + 224.0f * (c.cb_r * r + c.cb_g * g + c.cb_b * b) / 255.0f + 0.5f);
    const int v_val = static_cast<int>(128.5f
        + 224.0f * (c.cr_r * r + c.cr_g * g + c.cr_b * b) / 255.0f + 0.5f);
    return {
        .y = static_cast<uint8_t>(y_val < 0 ? 0 : (y_val > 255 ? 255 : y_val)),
        .u = static_cast<uint8_t>(u_val < 0 ? 0 : (u_val > 255 ? 255 : u_val)),
        .v = static_cast<uint8_t>(v_val < 0 ? 0 : (v_val > 255 ? 255 : v_val)),
    };
}

} // anonymous namespace

ReferenceYuvResult reference_convert_to_yuv420p(const ReferenceYuvRequest& req) {
    const int w = req.width;
    const int h = req.height;

    if (w <= 0 || h <= 0 || w % 2 != 0 || h % 2 != 0)
        return {};
    if (!req.planes.y || !req.planes.u || !req.planes.v)
        return {};

    const Framebuffer& src       = req.src;
    const int          src_stride = src.allocated_width();
    const Color*       src_data   = src.data();
    const auto&        coeffs     = get_coeffs(req.matrix);
    const bool         apply_gamma = req.apply_gamma;

    const int stride_y = req.planes.stride_y ? req.planes.stride_y : w;
    const int stride_u = req.planes.stride_u ? req.planes.stride_u : (w / 2);
    const int stride_v = req.planes.stride_v ? req.planes.stride_v : (w / 2);

    for (int block = 0; block < h / 2; ++block) {
        const int y = block * 2;

        const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
        const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

        uint8_t* y0 = req.planes.y + static_cast<size_t>(y) * stride_y;
        uint8_t* y1 = req.planes.y + static_cast<size_t>(y + 1) * stride_y;
        uint8_t* u_row = req.planes.u + static_cast<size_t>(block) * stride_u;
        uint8_t* v_row = req.planes.v + static_cast<size_t>(block) * stride_v;

        auto to8 = [apply_gamma](float v) -> uint8_t {
            return apply_gamma
                ? Color::linear_to_srgb8(v)
                : static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        for (int x = 0; x < w; x += 2) {
            const uint8_t r00 = to8(src_row0[x].r),     g00 = to8(src_row0[x].g),     b00 = to8(src_row0[x].b);
            const uint8_t r01 = to8(src_row0[x+1].r),   g01 = to8(src_row0[x+1].g),   b01 = to8(src_row0[x+1].b);
            const uint8_t r10 = to8(src_row1[x].r),     g10 = to8(src_row1[x].g),     b10 = to8(src_row1[x].b);
            const uint8_t r11 = to8(src_row1[x+1].r),   g11 = to8(src_row1[x+1].g),   b11 = to8(src_row1[x+1].b);

            const YuvPixel yuv00 = rgb8_to_yuv_ref(r00, g00, b00, coeffs);
            const YuvPixel yuv01 = rgb8_to_yuv_ref(r01, g01, b01, coeffs);
            const YuvPixel yuv10 = rgb8_to_yuv_ref(r10, g10, b10, coeffs);
            const YuvPixel yuv11 = rgb8_to_yuv_ref(r11, g11, b11, coeffs);

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

    return {.success = true, .error = ConversionError::None};
}

ReferenceYuvResult reference_convert_to_nv12(const ReferenceYuvRequest& req) {
    const int w = req.width;
    const int h = req.height;

    if (w <= 0 || h <= 0 || w % 2 != 0 || h % 2 != 0)
        return {.success = false, .error = ConversionError::OddDims};
    if (!req.planes.y || !req.planes.uv)
        return {.success = false, .error = ConversionError::NullPointer};

    const Framebuffer& src       = req.src;
    const int          src_stride = src.allocated_width();
    const Color*       src_data   = src.data();
    const auto&        coeffs     = get_coeffs(req.matrix);
    const bool         apply_gamma = req.apply_gamma;

    const int stride_y  = req.planes.stride_y  ? req.planes.stride_y  : w;
    const int stride_uv = req.planes.stride_uv ? req.planes.stride_uv : w;

    for (int block = 0; block < h / 2; ++block) {
        const int y = block * 2;

        const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
        const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

        uint8_t* y0 = req.planes.y + static_cast<size_t>(y) * stride_y;
        uint8_t* y1 = req.planes.y + static_cast<size_t>(y + 1) * stride_y;
        uint8_t* uv_row = req.planes.uv + static_cast<size_t>(block) * stride_uv;

        auto to8 = [apply_gamma](float v) -> uint8_t {
            return apply_gamma
                ? Color::linear_to_srgb8(v)
                : static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        for (int x = 0; x < w; x += 2) {
            const uint8_t r00 = to8(src_row0[x].r),     g00 = to8(src_row0[x].g),     b00 = to8(src_row0[x].b);
            const uint8_t r01 = to8(src_row0[x+1].r),   g01 = to8(src_row0[x+1].g),   b01 = to8(src_row0[x+1].b);
            const uint8_t r10 = to8(src_row1[x].r),     g10 = to8(src_row1[x].g),     b10 = to8(src_row1[x].b);
            const uint8_t r11 = to8(src_row1[x+1].r),   g11 = to8(src_row1[x+1].g),   b11 = to8(src_row1[x+1].b);

            const YuvPixel yuv00 = rgb8_to_yuv_ref(r00, g00, b00, coeffs);
            const YuvPixel yuv01 = rgb8_to_yuv_ref(r01, g01, b01, coeffs);
            const YuvPixel yuv10 = rgb8_to_yuv_ref(r10, g10, b10, coeffs);
            const YuvPixel yuv11 = rgb8_to_yuv_ref(r11, g11, b11, coeffs);

            y0[x]     = yuv00.y;
            y0[x + 1] = yuv01.y;
            y1[x]     = yuv10.y;
            y1[x + 1] = yuv11.y;

            const int uvx = x;
            uv_row[uvx]     = static_cast<uint8_t>(
                (static_cast<unsigned>(yuv00.u) + yuv01.u + yuv10.u + yuv11.u + 2) / 4);
            uv_row[uvx + 1] = static_cast<uint8_t>(
                (static_cast<unsigned>(yuv00.v) + yuv01.v + yuv10.v + yuv11.v + 2) / 4);
        }
    }

    return {.success = true, .error = ConversionError::None};
}

} // namespace chronon3d::video
