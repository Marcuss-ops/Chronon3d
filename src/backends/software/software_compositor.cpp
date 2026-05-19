#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/trace.hpp>
#include <immintrin.h>
#include <algorithm>

namespace chronon3d {

void SoftwareCompositor::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    if (mode == BlendMode::Normal) {
        if (composite_layer_normal_optimized(dst, src)) {
            return;
        }
    }

    const i32 w = dst.width(), h = dst.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

bool SoftwareCompositor::composite_layer_normal_optimized(Framebuffer& dst, const Framebuffer& src) {
    const i32 w = dst.width();
    const i32 h = dst.height();
    if (src.width() != w || src.height() != h) return false;

    // Track SIMD calls if profiling is active
    if (profiling::g_current_counters) {
        profiling::g_current_counters->simd_lerp_calls.fetch_add(1, std::memory_order_relaxed);
    }

    for (i32 y = 0; y < h; ++y) {
        Color* d_row = dst.pixels_row(y);
        const Color* s_row = src.pixels_row(y);
        i32 x = 0;

#if defined(__AVX2__) || defined(_MSC_VER)
        // Process 2 pixels at a time with AVX2
        for (; x + 1 < w; x += 2) {
            __m256 s = _mm256_loadu_ps(reinterpret_cast<const float*>(&s_row[x]));
            __m256 s_alpha = _mm256_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 3));
            
            // Mask where s_alpha > 0.0f
            __m256 zero = _mm256_setzero_ps();
            __m256 mask = _mm256_cmp_ps(s_alpha, zero, _CMP_GT_OQ);
            
            __m256 one = _mm256_set1_ps(1.0f);
            __m256 inv_a = _mm256_sub_ps(one, s_alpha);
            
            __m256 d = _mm256_loadu_ps(reinterpret_cast<const float*>(&d_row[x]));
            __m256 d_new = _mm256_add_ps(s, _mm256_mul_ps(d, inv_a));
            
            // Choose d_new if mask is active (s_alpha > 0.0f), else original d
            __m256 result = _mm256_blendv_ps(d, d_new, mask);
            
            _mm256_storeu_ps(reinterpret_cast<float*>(&d_row[x]), result);
        }
#endif

        // Clean up remaining pixel if w is odd
        for (; x < w; ++x) {
            const Color& s = s_row[x];
            if (s.a <= 0.0f) continue;
            Color& d = d_row[x];
            const float inv_a = 1.0f - s.a;
            d.r = s.r + d.r * inv_a;
            d.g = s.g + d.g * inv_a;
            d.b = s.b + d.b * inv_a;
            d.a = s.a + d.a * inv_a;
        }
    }
    return true;
}

} // namespace chronon3d
