#include "pip.hpp"
#include "path_utils.hpp"
#include <bit>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace chronon3d::renderer {

namespace {
    bool           s_pip_mode_set = false;
    bool           s_use_simd     = false;
    std::once_flag s_pip_mode_flag;
} // namespace

void set_pip_mode(bool use_simd) {
    std::call_once(s_pip_mode_flag, [&] {
        s_use_simd     = use_simd;
        s_pip_mode_set = true;
    });
}

PipMode get_pip_mode() {
    // If set_pip_mode() hasn't been called, the flag is false, and we
    // return the safe Scalar default (the same as when Config returns false).
    return s_use_simd ? PipMode::Simd : PipMode::Scalar;
}

bool point_in_polygon_even_odd(Vec2 p, const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return false;

    const bool prefetch = is_prefetch_enabled();
    bool inside = false;
    for (usize i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if (prefetch && i + 8 < poly.size()) {
            chrono_prefetch(&poly[i + 8]);
        }
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        const bool crosses =
            ((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + kPathEpsilon) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

#if defined(__AVX2__)
bool point_in_polygon_avx2(Vec2 p, const std::vector<Vec2>& poly) {
    const size_t n = poly.size();
    if (n < 3) return false;

    const bool prefetch = is_prefetch_enabled();
    uint32_t crossings = 0;
    __m256 py_vec = _mm256_set1_ps(p.y);
    __m256 px_vec = _mm256_set1_ps(p.x);
    __m256 eps_vec = _mm256_set1_ps(kPathEpsilon);

    alignas(32) float ax[8];
    alignas(32) float ay[8];
    alignas(32) float bx[8];
    alignas(32) float by[8];

    size_t edge_idx = 0;
    while (edge_idx < n) {
        if (prefetch && edge_idx + 8 < n) {
            chrono_prefetch(&poly[edge_idx + 8]);
        }
        size_t chunk = std::min(size_t(8), n - edge_idx);
        for (size_t c = 0; c < chunk; ++c) {
            size_t i = edge_idx + c;
            size_t j = (i == 0) ? n - 1 : i - 1;
            ax[c] = poly[i].x;
            ay[c] = poly[i].y;
            bx[c] = poly[j].x;
            by[c] = poly[j].y;
        }
        for (size_t c = chunk; c < 8; ++c) {
            ax[c] = 0.0f;
            ay[c] = p.y - 1.0f;
            bx[c] = 0.0f;
            by[c] = p.y - 1.0f;
        }

        __m256 ax_vec = _mm256_load_ps(ax);
        __m256 ay_vec = _mm256_load_ps(ay);
        __m256 bx_vec = _mm256_load_ps(bx);
        __m256 by_vec = _mm256_load_ps(by);

        __m256 ay_gt = _mm256_cmp_ps(ay_vec, py_vec, _CMP_GT_OQ);
        __m256 by_gt = _mm256_cmp_ps(by_vec, py_vec, _CMP_GT_OQ);
        __m256 y_cond = _mm256_xor_ps(ay_gt, by_gt);

        __m256 dy = _mm256_sub_ps(by_vec, ay_vec);
        __m256 dy_eps = _mm256_add_ps(dy, eps_vec);
        __m256 dx = _mm256_sub_ps(bx_vec, ax_vec);
        __m256 py_sub_ay = _mm256_sub_ps(py_vec, ay_vec);
        __m256 term = _mm256_mul_ps(dx, py_sub_ay);
        __m256 div_res = _mm256_div_ps(term, dy_eps);
        __m256 x_limit = _mm256_add_ps(div_res, ax_vec);
        __m256 x_cond = _mm256_cmp_ps(px_vec, x_limit, _CMP_LT_OQ);

        __m256 crosses_vec = _mm256_and_ps(y_cond, x_cond);
        int mask = _mm256_movemask_ps(crosses_vec);
        crossings += std::popcount(static_cast<unsigned int>(mask & 0xFF));

        edge_idx += chunk;
    }

    return (crossings % 2) != 0;
}
#endif

} // namespace chronon3d::renderer
