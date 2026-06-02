// ---------------------------------------------------------------------------
// effect_blur.cpp — Box-filter Gaussian blur implementation
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "effects_internal.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <cmath>

#if defined(__GNUC__) || defined(__clang__)
// Use GCC/Clang built-in prefetch
#define C3D_PREFETCH_READ(addr) __builtin_prefetch(reinterpret_cast<const void*>(addr), 0, 1)
#define C3D_PREFETCH_WRITE(addr) __builtin_prefetch(reinterpret_cast<const void*>(addr), 1, 1)
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#define C3D_PREFETCH_READ(addr)  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T1)
#define C3D_PREFETCH_WRITE(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T1)
#else
#define C3D_PREFETCH_READ(addr)  ((void)0)
#define C3D_PREFETCH_WRITE(addr) ((void)0)
#endif

namespace chronon3d {
namespace renderer {

/// Compute the equivalent 2-pass box-filter radius that produces approximately the same
/// Gaussian sigma as the standard 3-pass box filter with the given radius.
///
/// 3 passes of box filter radius r yield:  σ² = r² + r
/// 2 passes of box filter radius r2 yield: σ² = ⅔ · (r2² + r2)
/// Equating and solving for r2:
///   ⅔ · (r2² + r2) = r² + r
///   r2² + r2 = 1.5 · (r² + r)
///   r2 = ½ · (√(1 + 6·(r² + r)) − 1)
static i32 two_pass_equivalent_radius(i32 r) noexcept {
    const f64 r_plus = static_cast<f64>(r) * static_cast<f64>(r) + static_cast<f64>(r);  // r² + r
    const f64 r2 = 0.5 * (std::sqrt(1.0 + 6.0 * r_plus) - 1.0);
    return std::max(1, static_cast<i32>(std::round(r2)));
}

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip, int passes) {
    const i32 blur_passes = std::max(1, passes);

    // When using 2 passes, compute an equivalent radius so the output blur sigma
    // matches what the caller expects from the standard 3-pass box-filter.
    // The formula is derived from matching the Gaussian variance:  σ² = n · (r² + r) / 3
    const i32 r = (blur_passes == 2)
        ? two_pass_equivalent_radius(std::max(1, static_cast<i32>(std::round(radius))))
        : std::max(1, static_cast<i32>(std::round(radius)));

    const i32 w = fb.width(), h = fb.height();

    i32 x0 = 0, x1 = w;
    i32 y0 = 0, y1 = h;

    if (clip) {
        // N passes of box filter of radius r spread pixels up to N * r.
        // Expand bounds by N * r + 2 to contain the blur kernel footprint.
        const i32 margin = blur_passes * r + 2;
        x0 = std::clamp(clip->x0 - margin, 0, w);
        x1 = std::clamp(clip->x1 + margin, 0, w);
        y0 = std::clamp(clip->y0 - margin, 0, h);
        y1 = std::clamp(clip->y1 + margin, 0, h);
    }

    if (x0 >= x1 || y0 >= y1) return;

    auto tmp_fb = acquire_temp_framebuffer(w, h);
    Framebuffer& tmp = *tmp_fb;

    for (int pass = 0; pass < blur_passes; ++pass) {
        // Both horizontal and vertical passes must run on the exact same expanded bounding box
        // [x0, x1) x [y0, y1) to prevent reading unblurred/stale boundary pixels between passes.
        tbb::parallel_for(tbb::blocked_range<i32>(y0, y1), [&](const tbb::blocked_range<i32>& range) {
            for (i32 y = range.begin(); y < range.end(); ++y) {
                const Color* src_row = fb.pixels_row(y);
                Color* tmp_row = tmp.pixels_row(y);
                // Prefetch source and destination rows ahead to reduce cache misses
                if ((y & 15) == 0 && y + 16 < y1) {
                    C3D_PREFETCH_READ(fb.pixels_row(y + 16));
                    C3D_PREFETCH_WRITE(tmp.pixels_row(y + 16));
                }
                Color sum{0, 0, 0, 0};
                for (i32 x = x0 - r; x <= x0 + r; ++x) {
                    const Color p = src_row[std::clamp(x, 0, w - 1)];
                    sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
                }
                const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
                for (i32 x = x0; x < x1; ++x) {
                    tmp_row[x] = {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv};
                    const Color add = src_row[std::min(x + r + 1, w - 1)];
                    const Color rem = src_row[std::max(x - r,     0)];
                    sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                    sum.b += add.b - rem.b; sum.a += add.a - rem.a;
                }
            }
        });

        tbb::parallel_for(tbb::blocked_range<i32>(x0, x1), [&](const tbb::blocked_range<i32>& range) {
            for (i32 x = range.begin(); x < range.end(); ++x) {
                Color sum{0, 0, 0, 0};
                // Prefetch next column in tmp to warm cache for the vertical sweep
                if ((x & 15) == 0 && y0 + 16 < y1) {
                    C3D_PREFETCH_READ(&tmp.pixels_row(y0 + 16)[x]);
                }
                for (i32 y = y0 - r; y <= y0 + r; ++y) {
                    const Color p = tmp.pixels_row(std::clamp(y, 0, h - 1))[x];
                    sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
                }
                const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
                for (i32 y = y0; y < y1; ++y) {
                    Color* fb_row = fb.pixels_row(y);
                    fb_row[x] = {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv};
                    const Color add = tmp.pixels_row(std::min(y + r + 1, h - 1))[x];
                    const Color rem = tmp.pixels_row(std::max(y - r,     0))[x];
                    sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                    sum.b += add.b - rem.b; sum.a += add.a - rem.a;
                }
            }
        });
    }
}

} // namespace renderer
} // namespace chronon3d
