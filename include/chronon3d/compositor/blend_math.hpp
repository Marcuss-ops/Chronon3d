#pragma once

// ── Blend Math Reference ─────────────────────────────────────────────────────
//
// Canonical formulas for premultiplied-alpha compositing.
//
// Input convention (all functions in this header):
//   source premultiplied RGBA   — cs.rgb = source_straight * alpha
//   backdrop premultiplied RGBA — cb.rgb = backdrop_straight * alpha
//
// Output:
//   Co premultiplied RGBA
//
// Alpha output (same for all modes except Add):
//   Ao = As + Ab - As * Ab
//
// RGB premultiplied (Porter-Duff "over" + blend function B):
//   Co = cb * (1 - As) + cs * (1 - Ab) + B(Cb, Cs) * (As * Ab)
//
// See: https://www.w3.org/TR/compositing-1/

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::blend_math {

// ── Safe unpremultiply ──────────────────────────────────────────────────────
// Convert a premultiplied channel back to straight alpha.
// Returns 0 when alpha is near zero to avoid division-by-zero.

inline float safe_unpremultiply(float value, float alpha) {
    constexpr float epsilon = 1e-8f;
    return alpha > epsilon ? value / alpha : 0.0f;
}

// ── NaN/Inf guard ───────────────────────────────────────────────────────────

inline bool is_finite_color(const Color& c) {
    return  std::isfinite(c.r) && std::isfinite(c.g) &&
            std::isfinite(c.b) && std::isfinite(c.a);
}

// ── Blend function helpers (straight RGB) ───────────────────────────────────

inline float blend_darken(float cb, float cs) {
    return std::min(cb, cs);
}

inline float blend_lighten(float cb, float cs) {
    return std::max(cb, cs);
}

inline float blend_difference(float cb, float cs) {
    return std::abs(cb - cs);
}

inline float blend_exclusion(float cb, float cs) {
    return cb + cs - 2.0f * cb * cs;
}

// ── Advanced blend functions ────────────────────────────────────────────

inline float blend_hard_light(float cb, float cs) {
    if (cs <= 0.5f) return 2.0f * cb * cs;
    return 1.0f - 2.0f * (1.0f - cb) * (1.0f - cs);
}

inline float soft_light_d(float cb) {
    if (cb <= 0.25f) return ((16.0f * cb - 12.0f) * cb + 4.0f) * cb;
    return std::sqrt(cb);
}

inline float blend_soft_light(float cb, float cs) {
    if (cs <= 0.5f) {
        return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb);
    }
    return cb + (2.0f * cs - 1.0f) * (soft_light_d(cb) - cb);
}

inline float blend_color_dodge(float cb, float cs) {
    if (cs >= 1.0f) return 1.0f;
    if (cb <= 0.0f) return 0.0f;
    return std::min(1.0f, cb / std::max(1.0f - cs, 1e-8f));
}

inline float blend_color_burn(float cb, float cs) {
    if (cs <= 0.0f) return 0.0f;
    if (cb >= 1.0f) return 1.0f;
    return 1.0f - std::min(1.0f, (1.0f - cb) / std::max(cs, 1e-8f));
}

// ── Canonical premultiplied blend reference ─────────────────────────────────
// This is the scalar "source of truth" that all SIMD paths must match.
//
// Performs the full Porter-Duff "over" compositing with the blend function:
//   Co = cb * (1 - As) + cs * (1 - Ab) + B(Cb, Cs) * (As * Ab)

inline Color blend_reference_premul(
    const Color& source,
    const Color& backdrop,
    BlendMode mode)
{
    // NaN/Inf guard.
    if (!is_finite_color(source) || !is_finite_color(backdrop)) {
        return Color::transparent();
    }

    const float As = source.a;
    const float Ab = backdrop.a;

    // When source is fully transparent, result = backdrop unchanged.
    if (As <= 0.0f) return backdrop;

    // Alpha output (standard over).
    const float Ao = As + Ab - As * Ab;
    if (Ao <= 0.0f) return Color::transparent();

    // Un-premultiply to straight RGB for the blend function.
    const float cs_r = safe_unpremultiply(source.r, As);
    const float cs_g = safe_unpremultiply(source.g, As);
    const float cs_b = safe_unpremultiply(source.b, As);

    const float cb_r = safe_unpremultiply(backdrop.r, Ab);
    const float cb_g = safe_unpremultiply(backdrop.g, Ab);
    const float cb_b = safe_unpremultiply(backdrop.b, Ab);

    // Blend function B(Cb, Cs) — operates on straight RGB in [0,1].
    float br, bg, bb;

    switch (mode) {
        case BlendMode::Normal:
            br = cs_r; bg = cs_g; bb = cs_b;
            break;

        case BlendMode::Add:
            // Add is NOT the standard additive blend — it's pure HDR addition.
            // We preserve the engine's existing behaviour: result = source + backdrop.
            return {source.r + backdrop.r, source.g + backdrop.g,
                    source.b + backdrop.b, source.a + backdrop.a};

        case BlendMode::Multiply:
            br = cb_r * cs_r;
            bg = cb_g * cs_g;
            bb = cb_b * cs_b;
            break;

        case BlendMode::Screen:
            br = cb_r + cs_r - cb_r * cs_r;
            bg = cb_g + cs_g - cb_g * cs_g;
            bb = cb_b + cs_b - cb_b * cs_b;
            break;

        case BlendMode::Overlay:
            br = cb_r < 0.5f ? 2.0f * cs_r * cb_r : 1.0f - 2.0f * (1.0f - cs_r) * (1.0f - cb_r);
            bg = cb_g < 0.5f ? 2.0f * cs_g * cb_g : 1.0f - 2.0f * (1.0f - cs_g) * (1.0f - cb_g);
            bb = cb_b < 0.5f ? 2.0f * cs_b * cb_b : 1.0f - 2.0f * (1.0f - cs_b) * (1.0f - cb_b);
            break;

        case BlendMode::Darken:
            br = blend_darken(cb_r, cs_r);
            bg = blend_darken(cb_g, cs_g);
            bb = blend_darken(cb_b, cs_b);
            break;

        case BlendMode::Lighten:
            br = blend_lighten(cb_r, cs_r);
            bg = blend_lighten(cb_g, cs_g);
            bb = blend_lighten(cb_b, cs_b);
            break;

        case BlendMode::Difference:
            br = blend_difference(cb_r, cs_r);
            bg = blend_difference(cb_g, cs_g);
            bb = blend_difference(cb_b, cs_b);
            break;

        case BlendMode::Exclusion:
            br = blend_exclusion(cb_r, cs_r);
            bg = blend_exclusion(cb_g, cs_g);
            bb = blend_exclusion(cb_b, cs_b);
            break;

        // Advanced modes — clamp to [0,1] per HDR contract
        // (matches SIMD clamping in highway_color_kernels.cpp).
        case BlendMode::SoftLight:
            br = blend_soft_light(std::clamp(cb_r, 0.0f, 1.0f), std::clamp(cs_r, 0.0f, 1.0f));
            bg = blend_soft_light(std::clamp(cb_g, 0.0f, 1.0f), std::clamp(cs_g, 0.0f, 1.0f));
            bb = blend_soft_light(std::clamp(cb_b, 0.0f, 1.0f), std::clamp(cs_b, 0.0f, 1.0f));
            break;

        case BlendMode::HardLight:
            br = blend_hard_light(std::clamp(cb_r, 0.0f, 1.0f), std::clamp(cs_r, 0.0f, 1.0f));
            bg = blend_hard_light(std::clamp(cb_g, 0.0f, 1.0f), std::clamp(cs_g, 0.0f, 1.0f));
            bb = blend_hard_light(std::clamp(cb_b, 0.0f, 1.0f), std::clamp(cs_b, 0.0f, 1.0f));
            break;

        case BlendMode::ColorDodge:
            br = blend_color_dodge(std::clamp(cb_r, 0.0f, 1.0f), std::clamp(cs_r, 0.0f, 1.0f));
            bg = blend_color_dodge(std::clamp(cb_g, 0.0f, 1.0f), std::clamp(cs_g, 0.0f, 1.0f));
            bb = blend_color_dodge(std::clamp(cb_b, 0.0f, 1.0f), std::clamp(cs_b, 0.0f, 1.0f));
            break;

        case BlendMode::ColorBurn:
            br = blend_color_burn(std::clamp(cb_r, 0.0f, 1.0f), std::clamp(cs_r, 0.0f, 1.0f));
            bg = blend_color_burn(std::clamp(cb_g, 0.0f, 1.0f), std::clamp(cs_g, 0.0f, 1.0f));
            bb = blend_color_burn(std::clamp(cb_b, 0.0f, 1.0f), std::clamp(cs_b, 0.0f, 1.0f));
            break;

        default:
            br = cs_r; bg = cs_g; bb = cs_b;
            break;
    }

    // Porter-Duff "over" with blend function:
    //   Co = cb * (1 - As) + cs * (1 - Ab) + B(Cb, Cs) * (As * Ab)
    const float inv_As = 1.0f - As;
    const float inv_Ab = 1.0f - Ab;
    const float AsAb = As * Ab;

    Color result;
    result.r = (backdrop.r * inv_As) + (source.r * inv_Ab) + (br * AsAb);
    result.g = (backdrop.g * inv_As) + (source.g * inv_Ab) + (bg * AsAb);
    result.b = (backdrop.b * inv_As) + (source.b * inv_Ab) + (bb * AsAb);
    result.a = Ao;

    return result;
}

} // namespace chronon3d::blend_math
