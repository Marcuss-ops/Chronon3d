// ---------------------------------------------------------------------------
// effect_color.cpp — Brightness/contrast/tint color correction (AVX2 + scalar)
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "effects_internal.hpp"
#include <immintrin.h>
#include <algorithm>

namespace chronon3d {
namespace renderer {

namespace {

/// AVX2-accelerated brightness/contrast/tint pass.
/// Returns true if the pass ran (AVX2 available); false triggers scalar fallback.
inline bool apply_color_effects_avx2(Framebuffer& fb, const LayerEffect& effect) {
#if defined(__AVX2__)
    const bool needs_brightness_contrast = effect.brightness != 0.0f || effect.contrast != 1.0f;
    const bool needs_tint = effect.tint.a > 0.0f;
    if (!needs_brightness_contrast && !needs_tint) {
        return true;
    }

    const i32 w = fb.width();
    const i32 h = fb.height();
    const __m256 half         = _mm256_set1_ps(0.5f);
    const __m256 brightness   = _mm256_set1_ps(effect.brightness);
    const __m256 contrast     = _mm256_set1_ps(effect.contrast);
    const __m256 tint_mix     = _mm256_set1_ps(effect.tint.a);
    const __m256 inv_tint_mix = _mm256_set1_ps(1.0f - effect.tint.a);
    const __m256 tint_rgb     = _mm256_set_ps(effect.tint.a, effect.tint.b, effect.tint.g, effect.tint.r,
                                               effect.tint.a, effect.tint.b, effect.tint.g, effect.tint.r);

    for (i32 y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        i32 x = 0;
        for (; x + 1 < w; x += 2) {
            const __m256 src = _mm256_loadu_ps(reinterpret_cast<const float*>(row + x));
            __m256 work = src;
            if (needs_brightness_contrast) {
                work = _mm256_add_ps(work, brightness);
                work = _mm256_sub_ps(work, half);
                work = _mm256_mul_ps(work, contrast);
                work = _mm256_add_ps(work, half);
                work = _mm256_blend_ps(work, src, 0x88);  // preserve alpha
            }
            if (needs_tint) {
                work = _mm256_mul_ps(work, inv_tint_mix);
                work = _mm256_add_ps(work, _mm256_mul_ps(tint_rgb, tint_mix));
                work = _mm256_blend_ps(work, src, 0x88);  // preserve alpha
            }
            _mm256_storeu_ps(reinterpret_cast<float*>(row + x), work);
        }
        // Scalar tail
        for (; x < w; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;
            if (needs_brightness_contrast) {
                auto adj = [&](f32 v) {
                    return std::clamp((v + effect.brightness - 0.5f) * effect.contrast + 0.5f, 0.0f, 1.0f);
                };
                c.r = adj(c.r); c.g = adj(c.g); c.b = adj(c.b);
            }
            if (needs_tint) {
                const f32 t = effect.tint.a;
                c.r = c.r * (1.0f - t) + effect.tint.r * t;
                c.g = c.g * (1.0f - t) + effect.tint.g * t;
                c.b = c.b * (1.0f - t) + effect.tint.b * t;
            }
            row[x] = c;
        }
    }
    return true;
#else
    (void)fb; (void)effect;
    return false;
#endif
}

} // namespace

void apply_color_effects(Framebuffer& fb, const LayerEffect& effect,
                         const std::optional<raster::BBox>& clip) {
    if (apply_color_effects_avx2(fb, effect)) {
        return;
    }

    const i32 w = fb.width(), h = fb.height();
    i32 x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w);
        x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h);
        y1 = std::clamp(clip->y1, 0, h);
    }

    for (i32 y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            if (effect.brightness != 0.0f || effect.contrast != 1.0f) {
                auto adj = [&](f32 v) {
                    return std::clamp((v + effect.brightness - 0.5f) * effect.contrast + 0.5f, 0.0f, 1.0f);
                };
                c.r = adj(c.r); c.g = adj(c.g); c.b = adj(c.b);
            }
            if (effect.tint.a > 0.0f) {
                const f32 t = effect.tint.a;
                c.r = c.r * (1.0f - t) + effect.tint.r * t;
                c.g = c.g * (1.0f - t) + effect.tint.g * t;
                c.b = c.b * (1.0f - t) + effect.tint.b * t;
            }
            row[x] = c;
        }
    }
}

// ── Adjustment-layer color correction (AE-5) ────────────────────────────

void apply_saturation(Framebuffer& fb, f32 sat, const std::optional<raster::BBox>& clip) {
    if (sat == 1.0f) return;
    const i32 w = fb.width(), h = fb.height();
    i32 x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    for (i32 y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;
            const f32 lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            c.r = std::clamp(lum + sat * (c.r - lum), 0.0f, 1.0f);
            c.g = std::clamp(lum + sat * (c.g - lum), 0.0f, 1.0f);
            c.b = std::clamp(lum + sat * (c.b - lum), 0.0f, 1.0f);
            row[x] = c;
        }
    }
}

void apply_hue_rotate(Framebuffer& fb, f32 degrees, const std::optional<raster::BBox>& clip) {
    if (degrees == 0.0f) return;
    const f32 rad = degrees * (3.14159265f / 180.0f);
    const f32 cos_a = std::cos(rad);
    const f32 sin_a = std::sin(rad);
    const f32 s3 = 1.0f / std::sqrt(3.0f);
    const f32 w00 = cos_a + (1.0f - cos_a) / 3.0f;
    const f32 w01 = (1.0f - cos_a) / 3.0f - sin_a * s3;
    const f32 w02 = (1.0f - cos_a) / 3.0f + sin_a * s3;
    const f32 w10 = (1.0f - cos_a) / 3.0f + sin_a * s3;
    const f32 w11 = cos_a + (1.0f - cos_a) / 3.0f;
    const f32 w12 = (1.0f - cos_a) / 3.0f - sin_a * s3;
    const f32 w20 = (1.0f - cos_a) / 3.0f - sin_a * s3;
    const f32 w21 = (1.0f - cos_a) / 3.0f + sin_a * s3;
    const f32 w22 = cos_a + (1.0f - cos_a) / 3.0f;
    const i32 w = fb.width(), h = fb.height();
    i32 x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    for (i32 y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;
            const f32 r = c.r, g = c.g, b = c.b;
            c.r = std::clamp(w00 * r + w01 * g + w02 * b, 0.0f, 1.0f);
            c.g = std::clamp(w10 * r + w11 * g + w12 * b, 0.0f, 1.0f);
            c.b = std::clamp(w20 * r + w21 * g + w22 * b, 0.0f, 1.0f);
            row[x] = c;
        }
    }
}

void apply_invert(Framebuffer& fb, f32 amount, const std::optional<raster::BBox>& clip) {
    if (amount <= 0.0f) return;
    const i32 w = fb.width(), h = fb.height();
    i32 x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    for (i32 y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;
            c.r = c.r + amount * (1.0f - 2.0f * c.r);
            c.g = c.g + amount * (1.0f - 2.0f * c.g);
            c.b = c.b + amount * (1.0f - 2.0f * c.b);
            row[x] = c;
        }
    }
}

void apply_vignette(Framebuffer& fb, f32 radius, f32 softness, f32 amount,
                   Color color, const std::optional<raster::BBox>& clip) {
    if (amount <= 0.0f) return;
    const i32 w = fb.width(), h = fb.height();
    const f32 diag = std::sqrt(static_cast<f32>(w * w + h * h));
    const f32 cx = w * 0.5f;
    const f32 cy = h * 0.5f;
    const f32 inner_r = radius * diag * 0.5f;
    const f32 outer_r = inner_r + softness * diag * 0.5f;
    const f32 inv_range = 1.0f / std::max(1.0f, outer_r - inner_r);
    i32 x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    for (i32 y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;
            const f32 dx = static_cast<f32>(x) - cx;
            const f32 dy = static_cast<f32>(y) - cy;
            const f32 dist = std::sqrt(dx * dx + dy * dy);
            f32 vig = std::clamp((dist - inner_r) * inv_range, 0.0f, 1.0f);
            vig = vig * vig * (3.0f - 2.0f * vig);
            const f32 t = vig * amount;
            c.r = c.r * (1.0f - t) + color.r * t;
            c.g = c.g * (1.0f - t) + color.g * t;
            c.b = c.b * (1.0f - t) + color.b * t;
            row[x] = c;
        }
    }
}

} // namespace renderer
} // namespace chronon3d
