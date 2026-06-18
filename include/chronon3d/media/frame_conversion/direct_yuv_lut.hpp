#pragma once

// ---------------------------------------------------------------------------
// direct_yuv_lut.hpp — YUV coefficients, gamma LUT helpers and the
// conversion matrix used by the float-direct YUV converters.
//
// PR1: get_coeffs() now accepts YuvMatrix (3-way lookup).  BT.2020
// coefficients are defined for completeness but the float-direct
// codepath explicitly rejects BT.2020 today (returns UnsupportedMatrix)
// because Highway/TBB do not yet SIMD-vectorize the BT.2020 matrix; the
// swscale backend handles BT.2020 until a follow-up extension.
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <cstdint>

namespace chronon3d::video {

/// BT.601 (SDTV).
struct YuvCoeffs {
    float kr, kg, kb;        // Luma weights (kr + kg + kb = 1).
    float cb_r, cb_g, cb_b;  // Chroma Cb = cb_r*R + cb_g*G + cb_b*B.
    float cr_r, cr_g, cr_b;  // Chroma Cr = cr_r*R + cr_g*G + cr_b*B.
};

constexpr YuvCoeffs kCoeffsBT709{
    .kr = 0.2126f, .kg = 0.7152f, .kb = 0.0722f,
    .cb_r = -0.1146f, .cb_g = -0.3854f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.4542f, .cr_b = -0.0458f,
};

constexpr YuvCoeffs kCoeffsBT601{
    .kr = 0.299f, .kg = 0.587f, .kb = 0.114f,
    .cb_r = -0.168736f, .cb_g = -0.331264f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.418688f, .cr_b = -0.081312f,
};

constexpr YuvCoeffs kCoeffsBT2020{
    // BT.2020 NCL (Non-Constant Luminance).
    .kr = 0.2627f, .kg = 0.6780f, .kb = 0.0593f,
    .cb_r = -0.13963f, .cb_g = -0.36037f, .cb_b = 0.5f,
    .cr_r = 0.5f, .cr_g = -0.45979f, .cr_b = -0.04021f,
};

/// Deterministic lookup: never silently coerces BT.2020 to BT.709.
/// Direct backends must reject inputs ≠BT.601/BT.709 with UnsupportedMatrix.
inline const YuvCoeffs& get_coeffs(YuvMatrix matrix) {
    switch (matrix) {
        case YuvMatrix::BT601:  return kCoeffsBT601;
        case YuvMatrix::BT2020: return kCoeffsBT2020;
        case YuvMatrix::BT709:
        default:                return kCoeffsBT709;
    }
}

// Range offsets live in src/ (kernels) — keep them out of the public header.

// ── Single-pixel helpers (used by scalar reference implementation) ──

struct YuvPixel { uint8_t y; uint8_t u; uint8_t v; };

// Keep YuvCoeffs / get_coeffs / YuvPixel / rgb8_to_yuv above (PR1 contract).

// ============================================================================
//  PR1: 8-bit linear→sRGB LUT (defined in direct_yuv_converter.cpp, declared
//  here so the scalar and SIMD .cpp files can share the symbol).
//  `linear_to_srgb8_fast(float)` clamps to [0,1] and indexes the 64KB table.
//  Indexing into a 65536-entry LUT compresses the per-pixel gamma math from
//  ~8 floating-point ops to one load + clamp.
// ============================================================================

/// Aligned 8-bit gamma lookup — 64 KB.  C++ requires alignment
/// specifiers to be identical on both declaration and definition; this
/// header uses `alignas(64)` so TUs that include it see the alignment
/// promise before they reach the definition in direct_yuv_converter.cpp.
alignas(64) extern uint8_t g_srgb_lut[65536];

/// Aligned 32-bit variant for SIMD GatherIndex — 256 KB.
alignas(64) extern int32_t g_srgb_lut_u32[65536];

/// Clamped gamma-encode step.  Defined inline so both scalar (TBB) and SIMD
/// (.cpp + .hwy) compilers can inline it where appropriate.
inline uint8_t linear_to_srgb8_fast(float v) noexcept {
    const int idx = v <= 0.0f ? 0
                  : v >= 1.0f ? 65535
                  : static_cast<int>(v * 65535.0f + 0.5f);
    return g_srgb_lut[idx];
}


constexpr YuvPixel rgb8_to_yuv(
    uint8_t r, uint8_t g, uint8_t b, const YuvCoeffs& c) noexcept {
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

} // namespace chronon3d::video
