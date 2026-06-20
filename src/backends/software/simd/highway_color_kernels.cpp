// ── Dispatch and safe scalar fallbacks for SIMD color kernels ──────────────
//
// The SIMD implementations live in sibling Highway foreach_target files:
//   highway_blend_kernels.cpp       — Normal, Add, Multiply, Screen, Overlay
//   highway_separable_kernels.cpp   — Darken, Lighten, Difference, Exclusion,
//                                     SoftLight, HardLight, ColorDodge, ColorBurn
//   highway_conversion_kernels.cpp  — premultiply_alpha_rgba8, PRGB32 conversion,
//                                     alpha/luma matte
//
// This file provides: safe scalar fallbacks, NaN/Inf canary checks,
// and public API dispatch wrappers.

#include <hwy/highway.h>
#include <chronon3d/core/memory_utils.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/compositor/blend_math.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <span>

namespace chronon3d::simd {

// ── Safe scalar fallbacks for matte ─────────────────────────────────────

static void apply_alpha_matte_safe(std::span<Color> target, std::span<const Color> matte, bool inverted) {
    const int pixel_count = static_cast<int>(std::min(target.size(), matte.size()));
    for (int i = 0; i < pixel_count; ++i) {
        const float ma = matte[i].a;
        const float coverage = inverted ? (1.0f - ma) : ma;
        const float c = std::clamp(coverage, 0.0f, 1.0f);
        target[i].r *= c;
        target[i].g *= c;
        target[i].b *= c;
        target[i].a *= c;
    }
}

static void apply_luma_matte_safe(std::span<Color> target, std::span<const Color> matte, bool inverted) {
    const int pixel_count = static_cast<int>(std::min(target.size(), matte.size()));
    for (int i = 0; i < pixel_count; ++i) {
        const float luma = 0.2126f * matte[i].r + 0.7152f * matte[i].g + 0.0722f * matte[i].b;
        const float coverage = inverted ? (1.0f - luma) : luma;
        const float c = std::clamp(coverage, 0.0f, 1.0f);
        target[i].r *= c;
        target[i].g *= c;
        target[i].b *= c;
        target[i].a *= c;
    }
}

// Global flag: force scalar Normal blend for diagnostic purposes
std::atomic<bool> g_force_scalar_normal_blend{false};

/// Returns true if any channel (r,g,b,a) is NaN or Inf.
static bool has_bad_color(const Color& c) {
    return std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
           std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a);
}

/// Canary check on first + last pixel of both buffers.  Returns true if safe
/// for the fast SIMD path; falls back to a safe scalar loop when contaminated.
static bool check_nan_canary(std::span<const Color> src, std::span<const Color> dst) {
    if (src.empty() || dst.empty()) return false;
    return !has_bad_color(src[0]) && !has_bad_color(dst[0]) &&
           !has_bad_color(src.back()) && !has_bad_color(dst.back());
}

/// Safe scalar fallback for Normal blend (called when NaN/Inf detected).
static void composite_normal_premul_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        const Color& s = src[i];
        Color& d = dst[i];
        if (has_bad_color(s) || has_bad_color(d)) continue;
        if (s.a <= 0.0f) continue;
        const float inv_sa = 1.0f - s.a;
        d.r = s.r + d.r * inv_sa;
        d.g = s.g + d.g * inv_sa;
        d.b = s.b + d.b * inv_sa;
        d.a = s.a + d.a * inv_sa;
    }
}

void composite_normal_premul(std::span<Color> dst, std::span<const Color> src, bool force_scalar) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst) || force_scalar || g_force_scalar_normal_blend.load(std::memory_order_relaxed)) {
        composite_normal_premul_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_normal_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

// ── Non-normal blend modes (Add, Multiply, Screen, Overlay, Darken, etc.) ──

static void composite_add_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        d.r += s.r; d.g += s.g; d.b += s.b; d.a += s.a;
    }
}

static void composite_multiply_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r *= s.r; d.g *= s.g; d.b *= s.b; d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_screen_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = s.r + d.r - s.r * d.r;
        d.g = s.g + d.g - s.g * d.g;
        d.b = s.b + d.b - s.b * d.b;
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_overlay_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        const float sr = s.r, sg = s.g, sb = s.b;
        const float dr = d.r, dg = d.g, db = d.b;
        d.r = dr < 0.5f ? 2.0f * sr * dr : 1.0f - 2.0f * (1.0f - sr) * (1.0f - dr);
        d.g = dg < 0.5f ? 2.0f * sg * dg : 1.0f - 2.0f * (1.0f - sg) * (1.0f - dg);
        d.b = db < 0.5f ? 2.0f * sb * db : 1.0f - 2.0f * (1.0f - sb) * (1.0f - db);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

// ── Separable blend safe scalar fallbacks ──────────────────────────────────

static void composite_darken_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = std::min(s.r, d.r);
        d.g = std::min(s.g, d.g);
        d.b = std::min(s.b, d.b);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_lighten_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = std::max(s.r, d.r);
        d.g = std::max(s.g, d.g);
        d.b = std::max(s.b, d.b);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_difference_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = std::abs(s.r - d.r);
        d.g = std::abs(s.g - d.g);
        d.b = std::abs(s.b - d.b);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_exclusion_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = s.r + d.r - 2.0f * s.r * d.r;
        d.g = s.g + d.g - 2.0f * s.g * d.g;
        d.b = s.b + d.b - 2.0f * s.b * d.b;
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

void composite_add_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_add_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_add_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_multiply_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_multiply_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_multiply_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_screen_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_screen_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_screen_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_overlay_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_overlay_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_overlay_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_darken_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_darken_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_darken_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_lighten_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_lighten_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_lighten_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_difference_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_difference_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_difference_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_exclusion_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_exclusion_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_exclusion_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

// ── Separable-blend safe scalar fallbacks (SoftLight, HardLight, etc.) ─────

static void composite_soft_light_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_soft_light(Cb_r, Cs_r);
        const float bg = blend_math::blend_soft_light(Cb_g, Cs_g);
        const float bb = blend_math::blend_soft_light(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

static void composite_hard_light_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_hard_light(Cb_r, Cs_r);
        const float bg = blend_math::blend_hard_light(Cb_g, Cs_g);
        const float bb = blend_math::blend_hard_light(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

static void composite_color_dodge_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_color_dodge(Cb_r, Cs_r);
        const float bg = blend_math::blend_color_dodge(Cb_g, Cs_g);
        const float bb = blend_math::blend_color_dodge(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

static void composite_color_burn_safe(std::span<Color> dst, std::span<const Color> src) {
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_color_burn(Cb_r, Cs_r);
        const float bg = blend_math::blend_color_burn(Cb_g, Cs_g);
        const float bb = blend_math::blend_color_burn(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

void composite_soft_light_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_soft_light_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_soft_light_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_hard_light_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_hard_light_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_hard_light_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_color_dodge_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_color_dodge_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_color_dodge_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void composite_color_burn_premul(std::span<Color> dst, std::span<const Color> src) {
    if (dst.empty() || src.empty()) return;
    if (!check_nan_canary(src, dst)) {
        composite_color_burn_safe(dst, src);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(dst.size(), src.size()));
    composite_color_burn_premul_dispatch(reinterpret_cast<float*>(dst.data()), reinterpret_cast<const float*>(src.data()), pixel_count);
}

void apply_alpha_matte_premul(std::span<Color> target, std::span<const Color> matte, bool inverted) {
    if (target.empty() || matte.empty()) return;
    if (!check_nan_canary(matte, target)) {
        apply_alpha_matte_safe(target, matte, inverted);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(target.size(), matte.size()));
    apply_alpha_matte_premul_dispatch(reinterpret_cast<float*>(target.data()), reinterpret_cast<const float*>(matte.data()), pixel_count, inverted);
}

void apply_luma_matte_premul(std::span<Color> target, std::span<const Color> matte, bool inverted) {
    if (target.empty() || matte.empty()) return;
    if (!check_nan_canary(matte, target)) {
        apply_luma_matte_safe(target, matte, inverted);
        return;
    }
    const int pixel_count = static_cast<int>(std::min(target.size(), matte.size()));
    apply_luma_matte_premul_dispatch(reinterpret_cast<float*>(target.data()), reinterpret_cast<const float*>(matte.data()), pixel_count, inverted);
}

void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count) {
    premultiply_alpha_rgba8_dispatch(dst, src, pixel_count);
}

void bl_image_prgb32_to_color_row(Color* __restrict__ dst,
                                   const uint32_t* __restrict__ src,
                                   int pixel_count) {
    if (pixel_count <= 0) return;
    bl_image_prgb32_to_color_row_dispatch(
        reinterpret_cast<float*>(dst), src, pixel_count);
}

void color_to_prgb32_row(uint32_t* __restrict__ dst,
                          const Color* __restrict__ src,
                          int pixel_count) {
    if (pixel_count <= 0) return;
    color_to_prgb32_row_dispatch(
        dst, reinterpret_cast<const float*>(src), pixel_count);
}

}  // namespace chronon3d::simd
