#pragma once

// ---------------------------------------------------------------------------
// Output color transform layer.
//
// Internal renderer convention (unchanged):
//   - Framebuffer pixels stored in LinearSRGB, straight alpha.
//
// This header provides the output-side transform:
//   LinearSRGB (float RGBA) → OutputColorSpace (uint8_t RGB)
//
// Supported output spaces:
//   SRGB   — sRGB gamma (IEC 61966-2-1). The standard for PNG/web.
//   Rec709 — BT.709 gamma, common in HD video pipelines.
//
// Usage:
//   auto rgb = color::linear_to_output_rgb8(linear_color, options);
//   // rgb.r, rgb.g, rgb.b are uint8_t ready for encoding.
//
// Future: add ACEScg, Display P3, and LUT-based transforms here.
// ---------------------------------------------------------------------------

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace chronon3d {
namespace color {

// ---------------------------------------------------------------------------
// ColorSpace — specifies the output color encoding
// ---------------------------------------------------------------------------
enum class ColorSpace : uint8_t {
    LinearSRGB = 0,
    SRGB,        // sRGB gamma (IEC 61966-2-1)
    Rec709,      // BT.709 gamma
    ACEScg       // reserved for future use
};

// ---------------------------------------------------------------------------
// Rgb8 — 8-bit per channel RGB (no alpha)
// ---------------------------------------------------------------------------
struct Rgb8 {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
};

// ---------------------------------------------------------------------------
// OutputTransformOptions — configure the output transform
// ---------------------------------------------------------------------------
struct OutputTransformOptions {
    ColorSpace input{ColorSpace::LinearSRGB};
    ColorSpace output{ColorSpace::SRGB};  // default: sRGB for PNG output
    bool apply_gamma{true};
    bool clamp{true};
};

// ---------------------------------------------------------------------------
// Per-space single-channel helpers (linear → uint8_t)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Float-level helpers (no uint8 quantization)
// ---------------------------------------------------------------------------

/// Linear → sRGB gamma-encoded float [0,1] (IEEE 61966-2-1 OETF).
inline float linear_srgb_float(float v) {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return (v <= 0.0031308f)
        ? (v * 12.92f)
        : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
}

/// Linear → output-space float [0,1] without uint8 quantization.
/// Useful for YUV matrix pipelines where the uint8 round-trip
/// would lose precision.
inline float linear_to_output_float(float v, ColorSpace output, bool clamp = true) {
    if (clamp) {
        v = std::clamp(v, 0.0f, 1.0f);
    }
    switch (output) {
        case ColorSpace::SRGB:
        case ColorSpace::Rec709:
            return linear_srgb_float(v);
        case ColorSpace::LinearSRGB:
        default:
            return v;
    }
}

/// Apply the output transform to a full Color, returning gamma-encoded
/// float RGB [0,1] without uint8 quantization.
inline std::array<float, 3> linear_to_output_rgb_float(
    const Color& c, const OutputTransformOptions& opts)
{
    return {
        linear_to_output_float(c.r, opts.output, opts.clamp),
        linear_to_output_float(c.g, opts.output, opts.clamp),
        linear_to_output_float(c.b, opts.output, opts.clamp)
    };
}

// ---------------------------------------------------------------------------
// uint8 helpers
// ---------------------------------------------------------------------------

/// Linear → sRGB 8-bit (delegates to the existing LUT-based
/// Color::linear_to_srgb8 for binary-identical output).
inline uint8_t linear_to_srgb8(float v) {
    return static_cast<uint8_t>(Color::linear_to_srgb8(v));
}

/// Linear → Rec709 8-bit (BT.709 gamma, same OETF as sRGB but different primaries).
/// For V1 the gamma curve is identical to sRGB; the difference is in the
/// color primaries which require a matrix transform (future).
inline uint8_t linear_to_rec709_u8(float v) {
    return linear_to_srgb8(v);
}

/// Linear → output uint8_t based on the configured ColorSpace.
inline uint8_t linear_to_output_u8(float v, ColorSpace output) {
    switch (output) {
        case ColorSpace::SRGB:
        case ColorSpace::Rec709:
            return linear_to_srgb8(v);
        case ColorSpace::LinearSRGB:
            return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        default:
            return linear_to_srgb8(v);
    }
}

// ---------------------------------------------------------------------------
// Color-level transforms
// ---------------------------------------------------------------------------

/// Convert a linear Color to 8-bit RGB in the output color space.
inline Rgb8 linear_to_output_rgb8(const Color& c, const OutputTransformOptions& opts) {
    float r = c.r;
    float g = c.g;
    float b = c.b;

    if (opts.clamp) {
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);
    }

    if (opts.apply_gamma) {
        return Rgb8{
            linear_to_output_u8(r, opts.output),
            linear_to_output_u8(g, opts.output),
            linear_to_output_u8(b, opts.output)
        };
    }

    return Rgb8{
        static_cast<uint8_t>(r * 255.0f + 0.5f),
        static_cast<uint8_t>(g * 255.0f + 0.5f),
        static_cast<uint8_t>(b * 255.0f + 0.5f)
    };
}

/// Convenience: default options produce sRGB output.
inline Rgb8 linear_to_srgb_rgb8(const Color& c) {
    return linear_to_output_rgb8(c, OutputTransformOptions{});
}

/// Convenience: Rec709 output.
inline Rgb8 linear_to_rec709_rgb8(const Color& c) {
    OutputTransformOptions opts;
    opts.output = ColorSpace::Rec709;
    return linear_to_output_rgb8(c, opts);
}

} // namespace color
} // namespace chronon3d
