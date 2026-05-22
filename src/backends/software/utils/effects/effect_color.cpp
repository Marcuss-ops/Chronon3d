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
#if defined(__AVX2__) || defined(_MSC_VER)
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

} // namespace renderer
} // namespace chronon3d
