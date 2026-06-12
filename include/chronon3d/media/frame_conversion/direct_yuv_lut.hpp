#pragma once

// ---------------------------------------------------------------------------
// direct_yuv_lut.hpp — Shared sRGB gamma LUT + BT.709/601 helpers for
// both scalar (direct_yuv_converter.cpp) and HWY SIMD
// (direct_yuv_converter_hwy.cpp) direct YUV converters.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>

namespace chronon3d::video {

// ============================================================================
//  Shared sRGB gamma LUT
// ============================================================================

/// 64 KB sRGB gamma lookup table (uint8): index = uint16(linear_float * 65535).
/// Precomputed at startup via Color::linear_to_srgb8().  Defined in
/// direct_yuv_converter.cpp, declared extern here for the HWY variant.
alignas(64) extern uint8_t  g_srgb_lut[65536];

/// 256 KB sRGB gamma lookup table (uint32, mirrors g_srgb_lut).
/// Used by HWY GatherIndex which requires element size ≥ 4 bytes.
alignas(64) extern int32_t g_srgb_lut_u32[65536];

extern bool        g_srgb_lut_ready;

// (Removed: the 256-entry LUT was removed because it caused green tint
// in dark colors due to coarse quantization. The SIMD path now uses the
// full 65536-entry LUT for bit-exact accuracy with the scalar path.)

/// Fast LUT-based sRGB gamma encoding: linear float [0..1] → uint8 [0..255].
inline uint8_t linear_to_srgb8_fast(float v) {
    int idx = static_cast<int>(v * 65535.0f + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > 65535) idx = 65535;
    return g_srgb_lut[idx];
}

// ============================================================================
//  BT.709 / BT.601 limited-range YUV conversion constants & helpers
// ============================================================================

struct YuvCoeffs {
    float kr, kg, kb;           // luma coefficients
    float cb_r, cb_g, cb_b;     // Cb: R, G, B multipliers  (sum = 0 for neutral grey)
    float cr_r, cr_g, cr_b;     // Cr: R, G, B multipliers  (sum = 0 for neutral grey)
};

// BT.709-6 (2015) Table 4 — Y′CbCr from R′G′B′
//   Y  =  0.2126*R + 0.7152*G + 0.0722*B
//   Cb = -0.114572*R - 0.385428*G + 0.5*B
//   Cr =  0.5*R - 0.454153*G - 0.045847*B
//   → cb_r + cb_g + cb_b = 0, cr_r + cr_g + cr_b = 0
static constexpr YuvCoeffs kCoeffsBT709 = {
    .kr = 0.2126f, .kg = 0.7152f, .kb = 0.0722f,
    .cb_r = -0.114572f, .cb_g = -0.385428f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.454153f, .cr_b = -0.045847f,
};

// BT.601-7 (2011) — Y′CbCr from R′G′B′
//   Y  =  0.299*R + 0.587*G + 0.114*B
//   Cb = -0.168736*R - 0.331264*G + 0.5*B
//   Cr =  0.5*R - 0.418688*G - 0.081312*B
//   → cb_r + cb_g + cb_b = 0, cr_r + cr_g + cr_b = 0
static constexpr YuvCoeffs kCoeffsBT601 = {
    .kr = 0.2990f, .kg = 0.5870f, .kb = 0.1140f,
    .cb_r = -0.168736f, .cb_g = -0.331264f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.418688f, .cr_b = -0.081312f,
};

inline const YuvCoeffs& get_coeffs(int color_matrix) {
    return (color_matrix == 1) ? kCoeffsBT601 : kCoeffsBT709;
}

/// Convert a gamma-encoded 8-bit R,G,B triple (0..255) to limited-range YUV.
struct YuvPixel { uint8_t y, u, v; };

inline YuvPixel rgb8_to_yuv(uint8_t r, uint8_t g, uint8_t b, const YuvCoeffs& c) {
    const float rf = r / 255.0f;
    const float gf = g / 255.0f;
    const float bf = b / 255.0f;

    YuvPixel p;
    p.y = static_cast<uint8_t>(std::clamp(
        16.0f + 219.0f * (c.kr * rf + c.kg * gf + c.kb * bf) + 0.5f, 0.0f, 255.0f));
    p.u = static_cast<uint8_t>(std::clamp(
        128.0f + 224.0f * (c.cb_r * rf + c.cb_g * gf + c.cb_b * bf) + 0.5f, 0.0f, 255.0f));
    p.v = static_cast<uint8_t>(std::clamp(
        128.0f + 224.0f * (c.cr_r * rf + c.cr_g * gf + c.cr_b * bf) + 0.5f, 0.0f, 255.0f));
    return p;
}

} // namespace chronon3d::video
