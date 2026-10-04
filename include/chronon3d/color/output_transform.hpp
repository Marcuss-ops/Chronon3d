#pragma once

// ---------------------------------------------------------------------------
// Output color transform layer.
//
// Internal renderer convention (unchanged):
//   - Framebuffer pixels stored in linear BT.709/sRGB primaries, premultiplied
//     alpha. Color transforms operate on straight RGB values.
//
// This header provides the output-side transform:
//   source ColorSpace (float RGBA) → target ColorSpace
//
// Supported output spaces:
//   SRGB   — sRGB gamma (IEC 61966-2-1). The standard for PNG/web.
//   Rec709 — BT.709 transfer function and BT.709 primaries.
//   LinearBT2020 — linear-light BT.2020 primaries.
//   PQ / HLG — BT.2020 HDR signals; PQ uses 100 nits as working value 1.0.
//
// Usage:
//   auto rgb = color::linear_to_output_rgb8(linear_color, options);
//   // rgb.r, rgb.g, rgb.b are uint8_t ready for encoding.
//
// ACEScg is reserved for OCIO-backed transforms; builtin transforms fail
// closed for it rather than relabeling pixels as sRGB.
// ---------------------------------------------------------------------------

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <glm/gtc/color_space.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace chronon3d {
namespace color {

// ---------------------------------------------------------------------------
// ColorSpace — specifies the output color encoding
// ---------------------------------------------------------------------------
enum class ColorSpace : uint8_t {
    LinearSRGB = 0,
    SRGB,        // sRGB gamma (IEC 61966-2-1)
    Rec709,      // BT.709 gamma
    ACEScg,      // reserved; requires an OCIO transform
    LinearBT2020,
    PQ,          // ST 2084, BT.2020 primaries, 100 nits == working value 1
    HLG,         // BT.2100 scene transfer, BT.2020 primaries
};

/// Optional scene-referred compression before an SDR output transfer.
enum class ToneMapOperator : uint8_t {
    None = 0,
    Reinhard,
    AcesFitted,
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
    float reference_white_nits{100.0f};
    ToneMapOperator tone_map{ToneMapOperator::None};
    float exposure{1.0f};
};

[[nodiscard]] inline float tone_map_channel(float value, ToneMapOperator op,
                                            float exposure) {
    value = std::max(0.0f, value * exposure);
    switch (op) {
        case ToneMapOperator::None:
            return value;
        case ToneMapOperator::Reinhard:
            return value / (1.0f + value);
        case ToneMapOperator::AcesFitted: {
            constexpr float a = 2.51f;
            constexpr float b = 0.03f;
            constexpr float c = 2.43f;
            constexpr float d = 0.59f;
            constexpr float e = 0.14f;
            return std::clamp((value * (a * value + b)) /
                              (value * (c * value + d) + e), 0.0f, 1.0f);
        }
    }
    throw std::invalid_argument("unknown tone-map operator");
}

// ---------------------------------------------------------------------------
// Per-space single-channel helpers (linear → uint8_t)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Float-level helpers (no uint8 quantization)
// ---------------------------------------------------------------------------

/// Apply the extended sRGB OETF without implicit clipping. The standard
/// curve is defined on [0,1]; extending its linear segment below zero and
/// power segment above one preserves out-of-range values when a caller has
/// explicitly disabled clamping. Callers requesting bounded output clamp
/// before invoking this function.
inline float linear_srgb_float(float v) {
    return v <= 0.0031308f
        ? 12.92f * v
        : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

/// Linear-light Rec.709 OETF. Unlike the sRGB OETF, Rec.709 uses a linear
/// segment through 0.018 and a 0.45 power segment above it. Values above one
/// are preserved when the caller disables clipping, which keeps this helper
/// useful for scene-referred diagnostics and HDR-aware processing.
inline float linear_rec709_float(float v) {
    return v < 0.018f ? 4.5f * v
                      : 1.099f * std::pow(v, 0.45f) - 0.099f;
}

/// Decode an encoded Rec.709 signal into linear light.
inline float rec709_to_linear_float(float v) {
    return v < 0.081f ? v / 4.5f
                      : std::pow((v + 0.099f) / 1.099f, 1.0f / 0.45f);
}

inline float srgb_to_linear_float(float v) {
    return v <= 0.04045f ? v / 12.92f
                         : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

/// Decode an SMPTE ST 2084/PQ code value to linear light relative to 100 nits.
/// Chronon's working framebuffer is scene-linear; using 100 nits as unit 1.0
/// preserves HDR headroom above SDR white without clipping it at ingest.
inline float pq_to_linear_100_nits(float encoded) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float p = std::pow(std::clamp(encoded, 0.0f, 1.0f), 1.0f / m2);
    const float numerator = std::max(p - c1, 0.0f);
    const float denominator = c2 - c3 * p;
    if (denominator <= 0.0f) return 100.0f;
    const float normalized_luminance = std::pow(numerator / denominator, 1.0f / m1);
    return normalized_luminance * 100.0f;
}

/// Decode the HLG scene-referred inverse OETF. Display-system OOTF is left to
/// the output transform because the render graph works in scene-linear light.
inline float hlg_to_linear_scene(float encoded) {
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;
    const float value = std::clamp(encoded, 0.0f, 1.0f);
    return value <= 0.5f
        ? (value * value) / 3.0f
        : (std::exp((value - c) / a) + b) / 12.0f;
}

inline float linear_to_pq_100_nits(float linear) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float normalized_luminance = std::clamp(linear / 100.0f, 0.0f, 1.0f);
    const float p = std::pow(normalized_luminance, m1);
    return std::pow((c1 + c2 * p) / (1.0f + c3 * p), m2);
}

inline float linear_to_hlg_scene(float linear) {
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;
    const float value = std::max(linear, 0.0f);
    return value <= (1.0f / 12.0f)
        ? std::sqrt(3.0f * value)
        : a * std::log(12.0f * value - b) + c;
}

/// Convert linear BT.2020/D65 RGB into Chronon's linear BT.709/sRGB primaries.
[[nodiscard]] inline std::array<float, 3> bt2020_to_bt709_linear(
    float r, float g, float b) noexcept {
    return {
        1.660491f * r - 0.587641f * g - 0.072850f * b,
       -0.124550f * r + 1.132900f * g - 0.008349f * b,
       -0.018151f * r - 0.100579f * g + 1.118730f * b,
    };
}

/// Convert linear SMPTE 170M (525-line SD / SMPTE-C) RGB to BT.709 primaries.
/// Both spaces use D65; transfer decoding must happen before this matrix.
[[nodiscard]] inline std::array<float, 3> smpte170m_to_bt709_linear(
    float r, float g, float b) noexcept {
    return {
        0.93954206f * r + 0.05018136f * g + 0.01027658f * b,
        0.01777222f * r + 0.96579286f * g + 0.01643491f * b,
       -0.00162160f * r - 0.00436975f * g + 1.00599135f * b,
    };
}

/// Convert linear BT.470BG (625-line SD / EBU) RGB to BT.709 primaries.
/// Both spaces use D65; transfer decoding must happen before this matrix.
[[nodiscard]] inline std::array<float, 3> bt470bg_to_bt709_linear(
    float r, float g, float b) noexcept {
    return {
        1.04404321f * r - 0.04404321f * g,
        g,
        0.01179338f * g + 0.98820662f * b,
    };
}

/// Convert linear Display P3/D65 RGB to Chronon's linear BT.709 primaries.
[[nodiscard]] inline std::array<float, 3> display_p3_to_bt709_linear(
    float r, float g, float b) noexcept {
    return {
        1.22494018f * r - 0.22494018f * g,
       -0.04205695f * r + 1.04205695f * g,
       -0.01963755f * r - 0.07863605f * g + 1.09827360f * b,
    };
}

[[nodiscard]] inline std::array<float, 3> bt709_to_bt2020_linear(
    float r, float g, float b) noexcept {
    return {
        0.627404f * r + 0.329283f * g + 0.0433136f * b,
        0.069097f * r + 0.919540f * g + 0.0113612f * b,
        0.0163916f * r + 0.0880132f * g + 0.895595f * b,
    };
}

[[nodiscard]] inline bool is_builtin_color_space(ColorSpace space) noexcept {
    return space != ColorSpace::ACEScg;
}

[[nodiscard]] inline std::array<float, 3> to_linear_bt709(
    const Color& color, ColorSpace input, float reference_white_nits) {
    if (!is_builtin_color_space(input))
        throw std::invalid_argument("ACEScg requires the OCIO color transform");
    if (!std::isfinite(reference_white_nits) || reference_white_nits <= 0.0f)
        throw std::invalid_argument("reference_white_nits must be finite and positive");
    std::array<float, 3> rgb{color.r, color.g, color.b};
    switch (input) {
        case ColorSpace::LinearSRGB:
            return rgb;
        case ColorSpace::SRGB:
            for (auto& value : rgb) value = srgb_to_linear_float(value);
            return rgb;
        case ColorSpace::Rec709:
            for (auto& value : rgb) value = rec709_to_linear_float(value);
            return rgb;
        case ColorSpace::LinearBT2020:
            break;
        case ColorSpace::PQ:
            for (auto& value : rgb)
                value = pq_to_linear_100_nits(value) * (100.0f / reference_white_nits);
            break;
        case ColorSpace::HLG:
            for (auto& value : rgb) value = hlg_to_linear_scene(value);
            break;
        case ColorSpace::ACEScg:
            throw std::invalid_argument("ACEScg requires the OCIO color transform");
    }
    return bt2020_to_bt709_linear(rgb[0], rgb[1], rgb[2]);
}

[[nodiscard]] inline std::array<float, 3> from_linear_bt709(
    std::array<float, 3> rgb, ColorSpace output, float reference_white_nits,
    bool apply_transfer, bool clamp) {
    if (!is_builtin_color_space(output))
        throw std::invalid_argument("ACEScg requires the OCIO color transform");
    if (!std::isfinite(reference_white_nits) || reference_white_nits <= 0.0f)
        throw std::invalid_argument("reference_white_nits must be finite and positive");
    if (output == ColorSpace::LinearBT2020 || output == ColorSpace::PQ ||
        output == ColorSpace::HLG)
        rgb = bt709_to_bt2020_linear(rgb[0], rgb[1], rgb[2]);
    if (!apply_transfer) {
        if (clamp) for (auto& value : rgb) value = std::clamp(value, 0.0f, 1.0f);
        return rgb;
    }
    for (auto& value : rgb) {
        switch (output) {
            case ColorSpace::LinearSRGB:
            case ColorSpace::LinearBT2020:
                break;
            case ColorSpace::SRGB:
                value = linear_srgb_float(clamp ? std::clamp(value, 0.0f, 1.0f) : value);
                break;
            case ColorSpace::Rec709:
                value = linear_rec709_float(clamp ? std::clamp(value, 0.0f, 1.0f) : value);
                break;
            case ColorSpace::PQ:
                value = linear_to_pq_100_nits(
                    value * (reference_white_nits / 100.0f));
                break;
            case ColorSpace::HLG:
                value = linear_to_hlg_scene(value);
                break;
            case ColorSpace::ACEScg:
                throw std::invalid_argument("ACEScg requires the OCIO color transform");
        }
        if (clamp) value = std::clamp(value, 0.0f, 1.0f);
    }
    return rgb;
}

/// Linear → output-space float [0,1] without uint8 quantization.
/// Useful for YUV matrix pipelines where the uint8 round-trip
/// would lose precision.
inline float linear_to_output_float(float v, ColorSpace output, bool clamp = true) {
    const bool hdr_transfer = output == ColorSpace::PQ || output == ColorSpace::HLG;
    if (clamp && !hdr_transfer) {
        v = std::clamp(v, 0.0f, 1.0f);
    }
    float result = v;
    switch (output) {
        case ColorSpace::SRGB:
            result = linear_srgb_float(v);
            break;
        case ColorSpace::Rec709:
            result = linear_rec709_float(v);
            break;
        case ColorSpace::PQ:
            result = linear_to_pq_100_nits(v);
            break;
        case ColorSpace::HLG:
            result = linear_to_hlg_scene(v);
            break;
        case ColorSpace::LinearBT2020:
        case ColorSpace::LinearSRGB:
            result = v;
            break;
        case ColorSpace::ACEScg:
            throw std::invalid_argument("ACEScg requires the OCIO color transform");
    }
    return clamp ? std::clamp(result, 0.0f, 1.0f) : result;
}

/// Apply the output transform to a full Color, returning gamma-encoded
/// float RGB [0,1] without uint8 quantization.
inline std::array<float, 3> linear_to_output_rgb_float(
    const Color& c, const OutputTransformOptions& opts)
{
    if (!std::isfinite(opts.exposure) || opts.exposure < 0.0f || opts.exposure > 64.0f)
        throw std::invalid_argument("output exposure must be finite and in [0, 64]");
    auto linear = to_linear_bt709(c, opts.input, opts.reference_white_nits);
    for (auto& value : linear)
        value = tone_map_channel(value, opts.tone_map, opts.exposure);
    return from_linear_bt709(linear, opts.output, opts.reference_white_nits,
                             opts.apply_gamma, opts.clamp);
}

// ---------------------------------------------------------------------------
// uint8 helpers
// ---------------------------------------------------------------------------

/// Linear → sRGB 8-bit (delegates to the existing LUT-based
/// Color::linear_to_srgb8 for binary-identical output).
inline uint8_t linear_to_srgb8(float v) {
    return static_cast<uint8_t>(Color::linear_to_srgb8(v));
}

/// Linear → Rec.709 8-bit. Chronon's linear working primaries are sRGB/BT.709
/// D65, so this output boundary needs the BT.709 OETF but no primary matrix.
inline uint8_t linear_to_rec709_u8(float v) {
    const float encoded = linear_rec709_float(std::clamp(v, 0.0f, 1.0f));
    return static_cast<uint8_t>(std::clamp(std::round(encoded * 255.0f), 0.0f, 255.0f));
}

/// Linear → output uint8_t based on the configured ColorSpace.
inline uint8_t linear_to_output_u8(float v, ColorSpace output) {
    switch (output) {
        case ColorSpace::SRGB:
            return linear_to_srgb8(v);
        case ColorSpace::Rec709:
            return linear_to_rec709_u8(v);
        case ColorSpace::LinearSRGB:
            return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        case ColorSpace::ACEScg:
            throw std::invalid_argument("ACEScg requires the OCIO color transform");
        case ColorSpace::LinearBT2020:
        case ColorSpace::PQ:
        case ColorSpace::HLG:
            throw std::invalid_argument("HDR/wide-gamut output requires the float or 10-bit path");
    }
}

// ---------------------------------------------------------------------------
// Color-level transforms
// ---------------------------------------------------------------------------

/// Convert a linear Color to 8-bit RGB in the output color space.
inline Rgb8 linear_to_output_rgb8(const Color& c, const OutputTransformOptions& opts) {
    if (opts.output == ColorSpace::PQ || opts.output == ColorSpace::HLG ||
        opts.output == ColorSpace::LinearBT2020 || opts.output == ColorSpace::ACEScg)
        throw std::invalid_argument("target color space cannot be represented by RGB8");
    if (opts.input == ColorSpace::LinearSRGB && opts.output == ColorSpace::SRGB &&
        opts.apply_gamma && opts.tone_map == ToneMapOperator::None && opts.exposure == 1.0f) {
        const auto channel = [&](float value) {
            return static_cast<uint8_t>(Color::linear_to_srgb8(
                opts.clamp ? std::clamp(value, 0.0f, 1.0f) : value));
        };
        return {channel(c.r), channel(c.g), channel(c.b)};
    }
    const auto values = linear_to_output_rgb_float(c, opts);
    const auto channel = [](float value) {
        if (!std::isfinite(value)) return static_cast<uint8_t>(0);
        return static_cast<uint8_t>(std::clamp(std::round(value * 255.0f), 0.0f, 255.0f));
    };
    return {channel(values[0]), channel(values[1]), channel(values[2])};
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
