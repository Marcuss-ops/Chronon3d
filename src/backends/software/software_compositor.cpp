#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/trace.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <immintrin.h>
#include <algorithm>
#include <optional>

namespace chronon3d {

void SoftwareCompositor::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip) {
    i32 x0 = 0, y0 = 0, x1 = dst.width(), y1 = dst.height();
    if (clip) {
        x0 = std::clamp(clip->x0, 0, x1);
        y0 = std::clamp(clip->y0, 0, y1);
        x1 = std::clamp(clip->x1, 0, x1);
        y1 = std::clamp(clip->y1, 0, y1);
    }
    
    if (x0 >= x1 || y0 >= y1) return;

    if (mode == BlendMode::Normal) {
        if (composite_layer_normal_optimized(dst, src, x0, y0, x1, y1)) {
            return;
        }
    }

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

bool SoftwareCompositor::composite_layer_normal_optimized(Framebuffer& dst, const Framebuffer& src, i32 x0, i32 y0, i32 x1, i32 y1) {
    if (src.width() != dst.width() || src.height() != dst.height()) return false;

    // Track SIMD calls if profiling is active
    if (profiling::g_current_counters) {
        profiling::g_current_counters->simd_lerp_calls.fetch_add(1, std::memory_order_relaxed);
    }

    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* d_row = dst.pixels_row(y) + x0;
            const Color* s_row = src.pixels_row(y) + x0;
            i32 lx = 0;

#if defined(__AVX2__) || defined(_MSC_VER)
            // Process 2 pixels at a time with AVX2
            for (; lx + 1 < width_to_process; lx += 2) {
                __m256 s = _mm256_loadu_ps(reinterpret_cast<const float*>(&s_row[lx]));
                __m256 s_alpha = _mm256_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 3));
                
                __m256 zero = _mm256_setzero_ps();
                __m256 mask = _mm256_cmp_ps(s_alpha, zero, _CMP_GT_OQ);
                
                __m256 one = _mm256_set1_ps(1.0f);
                __m256 inv_a = _mm256_sub_ps(one, s_alpha);
                
                __m256 d = _mm256_loadu_ps(reinterpret_cast<const float*>(&d_row[lx]));
                __m256 d_new = _mm256_add_ps(s, _mm256_mul_ps(d, inv_a));
                
                __m256 result = _mm256_blendv_ps(d, d_new, mask);
                _mm256_storeu_ps(reinterpret_cast<float*>(&d_row[lx]), result);
            }
#endif

            for (; lx < width_to_process; ++lx) {
                const Color& s = s_row[lx];
                if (s.a <= 0.0f) continue;
                Color& d = d_row[lx];
                const float inv_a = 1.0f - s.a;
                d.r = s.r + d.r * inv_a;
                d.g = s.g + d.g * inv_a;
                d.b = s.b + d.b * inv_a;
                d.a = s.a + d.a * inv_a;
            }
        }
    };

    if (height_to_process >= 32) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                process_rows(range.begin(), range.end());
            }
        );
    } else {
        process_rows(y0, y1);
    }

    return true;
}

} // namespace chronon3d
