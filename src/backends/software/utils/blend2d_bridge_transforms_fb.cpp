// blend2d_bridge_transforms_fb.cpp
// Transformed compositing of Framebuffer → Framebuffer (bilinear, affine, projective).
// BLImage transforms live in blend2d_bridge_transforms.cpp;
// core helpers live in blend2d_bridge_core.cpp.

#include "blend2d_bridge.hpp"
#include "blend2d_bridge_detail.hpp"
#include <chronon3d/scene/model/mask_utils.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace chronon3d::blend2d_bridge {

// ── Framebuffer transformed ────────────────────────────────────────

void composite_framebuffer_transformed(Framebuffer& dst_fb, const Framebuffer& src_fb, const Mat4& model, float opacity, BlendMode mode, const RenderState* state) {
    const int sw = src_fb.width();
    const int sh = src_fb.height();
    const int src_stride = src_fb.stride();
    const Color* src_base = src_fb.data();

    if (detail::is_simple_translation(model)) {
        int tx = static_cast<int>(std::round(model[3][0]));
        int ty = static_cast<int>(std::round(model[3][1]));
        composite_framebuffer(dst_fb, src_fb, tx, ty, opacity, mode, state);
        return;
    }

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];
    const bool downscale_x = std::abs(sx) < 0.999f;
    const bool downscale_y = std::abs(sy) < 0.999f;
    const float sample_ox = downscale_x ? 0.25f / std::max(std::abs(sx), 1e-6f) : 0.0f;
    const float sample_oy = downscale_y ? 0.25f / std::max(std::abs(sy), 1e-6f) : 0.0f;

    auto sample_source = [&](float lx, float ly, Color& src) {
        if (!downscale_x && !downscale_y) {
            sample_bilinear_float(src_base, src_stride, sw, sh, lx, ly, src);
            return;
        }

        const float sx0 = downscale_x ? (lx - sample_ox) : lx;
        const float sx1 = downscale_x ? (lx + sample_ox) : lx;
        const float sy0 = downscale_y ? (ly - sample_oy) : ly;
        const float sy1 = downscale_y ? (ly + sample_oy) : ly;

        Color c0, c1, c2, c3;
        sample_bilinear_float(src_base, src_stride, sw, sh, sx0, sy0, c0);
        sample_bilinear_float(src_base, src_stride, sw, sh, sx1, sy0, c1);
        sample_bilinear_float(src_base, src_stride, sw, sh, sx0, sy1, c2);
        sample_bilinear_float(src_base, src_stride, sw, sh, sx1, sy1, c3);

        src.r = (c0.r + c1.r + c2.r + c3.r) * 0.25f;
        src.g = (c0.g + c1.g + c2.g + c3.g) * 0.25f;
        src.b = (c0.b + c1.b + c2.b + c3.b) * 0.25f;
        src.a = (c0.a + c1.a + c2.a + c3.a) * 0.25f;
    };

    if (detail::is_scale_translation(model)) {
        if (state && state->mask && state->mask->enabled()) {
            ensure_mask_alpha_cache(*state, dst_fb.width(), dst_fb.height());
        }

        float min_x = tx;
        float max_x = static_cast<float>(sw) * sx + tx;
        if (sx < 0.0f) std::swap(min_x, max_x);

        float min_y = ty;
        float max_y = static_cast<float>(sh) * sy + ty;
        if (sy < 0.0f) std::swap(min_y, max_y);

        const int x0_st = std::max<int>(0, static_cast<int>(std::floor(min_x)));
        const int y0_st = std::max<int>(0, static_cast<int>(std::floor(min_y)));
        const int x1_st = std::min<int>(dst_fb.width(),  static_cast<int>(std::ceil(max_x)));
        const int y1_st = std::min<int>(dst_fb.height(), static_cast<int>(std::ceil(max_y)));

        if (x0_st < x1_st && y0_st < y1_st) {
            const float inv_sx = 1.0f / sx;
            const float inv_sy = 1.0f / sy;

            auto process_rows_st = [&](int row_begin, int row_end) {
                for (int y = row_begin; y < row_end; ++y) {
                    Color* dst_row = dst_fb.pixels_row(y);
                    const float ly = (static_cast<float>(y) + 0.5f - ty) * inv_sy;
                    if (ly < 0.0f || ly >= static_cast<float>(sh)) continue;

                    const float vy = ly - 0.5f;
                    int sy0 = static_cast<int>(std::floor(vy));
                    const float ty_factor = vy - static_cast<float>(sy0);
                    int sy1 = sy0 + 1;
                    sy0 = std::clamp(sy0, 0, sh - 1);
                    sy1 = std::clamp(sy1, 0, sh - 1);
                    const float wyb = 1.0f - ty_factor;
                    const float wyt = ty_factor;
                    const Color* src_row0 = src_base + sy0 * src_stride;
                    const Color* src_row1 = src_base + sy1 * src_stride;

                    int x = x0_st;
#if defined(__AVX2__)
                    if (!downscale_x && !downscale_y) for (; x + 1 < x1_st; x += 2) {
                        const bool mask0 = !state || pixel_passes_mask(*state, x, y);
                        const bool mask1 = !state || pixel_passes_mask(*state, x + 1, y);

                        Color src[2];

                        if (mask0) {
                            const float lx0 = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                            if (lx0 >= 0.0f && lx0 < static_cast<float>(sw)) {
                                const float vx0 = lx0 - 0.5f;
                                int sx00 = static_cast<int>(std::floor(vx0));
                                const float tx0 = vx0 - static_cast<float>(sx00);
                                int sx01 = sx00 + 1;
                                sx00 = std::clamp(sx00, 0, sw - 1);
                                sx01 = std::clamp(sx01, 0, sw - 1);
                                const Color& c000 = src_row0[sx00];
                                const Color& c001 = src_row0[sx01];
                                const Color& c010 = src_row1[sx00];
                                const Color& c011 = src_row1[sx01];
                                const float w00 = (1.0f - tx0) * wyb;
                                const float w01 = tx0 * wyb;
                                const float w02 = (1.0f - tx0) * wyt;
                                const float w03 = tx0 * wyt;
                                src[0].r = c000.r * w00 + c001.r * w01 + c010.r * w02 + c011.r * w03;
                                src[0].g = c000.g * w00 + c001.g * w01 + c010.g * w02 + c011.g * w03;
                                src[0].b = c000.b * w00 + c001.b * w01 + c010.b * w02 + c011.b * w03;
                                src[0].a = c000.a * w00 + c001.a * w01 + c010.a * w02 + c011.a * w03;
                                src[0].r *= opacity; src[0].g *= opacity;
                                src[0].b *= opacity; src[0].a *= opacity;
                            } else {
                                src[0] = Color{0, 0, 0, 0};
                            }
                        } else {
                            src[0] = Color{0, 0, 0, 0};
                        }

                        if (mask1) {
                            const float lx1 = (static_cast<float>(x + 1) + 0.5f - tx) * inv_sx;
                            if (lx1 >= 0.0f && lx1 < static_cast<float>(sw)) {
                                const float vx1 = lx1 - 0.5f;
                                int sx10 = static_cast<int>(std::floor(vx1));
                                const float tx1 = vx1 - static_cast<float>(sx10);
                                int sx11 = sx10 + 1;
                                sx10 = std::clamp(sx10, 0, sw - 1);
                                sx11 = std::clamp(sx11, 0, sw - 1);
                                const Color& c100 = src_row0[sx10];
                                const Color& c101 = src_row0[sx11];
                                const Color& c110 = src_row1[sx10];
                                const Color& c111 = src_row1[sx11];
                                const float w10 = (1.0f - tx1) * wyb;
                                const float w11 = tx1 * wyb;
                                const float w12 = (1.0f - tx1) * wyt;
                                const float w13 = tx1 * wyt;
                                src[1].r = c100.r * w10 + c101.r * w11 + c110.r * w12 + c111.r * w13;
                                src[1].g = c100.g * w10 + c101.g * w11 + c110.g * w12 + c111.g * w13;
                                src[1].b = c100.b * w10 + c101.b * w11 + c110.b * w12 + c111.b * w13;
                                src[1].a = c100.a * w10 + c101.a * w11 + c110.a * w12 + c111.a * w13;
                                src[1].r *= opacity; src[1].g *= opacity;
                                src[1].b *= opacity; src[1].a *= opacity;
                            } else {
                                src[1] = Color{0, 0, 0, 0};
                            }
                        } else {
                            src[1] = Color{0, 0, 0, 0};
                        }

                        if (src[0].a <= 0.001f && src[1].a <= 0.001f) continue;

                        __m256 src_v = _mm256_loadu_ps(&src[0].r);
                        __m256 dst_v = _mm256_loadu_ps(&dst_row[x].r);

                        if (mode == BlendMode::Add) {
                            dst_v = _mm256_min_ps(_mm256_add_ps(dst_v, src_v), _mm256_set1_ps(1.0f));
                        } else {
                            const __m256 sa = _mm256_shuffle_ps(src_v, src_v, 0xFF);
                            const __m256 inv_sa = _mm256_sub_ps(_mm256_set1_ps(1.0f), sa);
                            dst_v = _mm256_add_ps(_mm256_mul_ps(dst_v, inv_sa), src_v);
                        }
                        _mm256_storeu_ps(&dst_row[x].r, dst_v);
                    }
#endif
                    for (; x < x1_st; ++x) {
                        if (state && !pixel_passes_mask(*state, x, y)) continue;

                        const float lx = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                        if (lx < 0.0f || lx >= static_cast<float>(sw)) continue;

                        Color src;
                        sample_source(lx, ly, src);
                        src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                        if (src.a <= 0.001f) continue;

                        detail::blend_pixel(dst_row[x], src, mode);
                    }
                }
            };

            if (y1_st - y0_st >= 16) {
                tbb::parallel_for(tbb::blocked_range<int>(y0_st, y1_st), [&](const tbb::blocked_range<int>& range) {
                    process_rows_st(range.begin(), range.end());
                });
            } else {
                process_rows_st(y0_st, y1_st);
            }
        }
        return;
    }

    // Full projective path
    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, dst_fb.width(), dst_fb.height());
    }

    const detail::TransformInfo ti(model);

    int x0, y0, x1, y1;
    detail::get_projective_bounds(model, static_cast<float>(sw), static_cast<float>(sh), dst_fb.width(), dst_fb.height(), x0, y0, x1, y1);
    if (x0 >= x1 || y0 >= y1) return;

    auto process_rows = [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            Color* dst_row = dst_fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (state && !pixel_passes_mask(*state, x, y)) continue;

                float lx = 0.0f;
                float ly = 0.0f;
                if (!ti.map(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, lx, ly)) continue;

                if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(sw) || ly >= static_cast<float>(sh)) continue;

                Color src;
                sample_source(lx, ly, src);
                src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                if (src.a <= 0.001f) continue;

                detail::blend_pixel(dst_row[x], src, mode);
            }
        }
    };

    if (y1 - y0 >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(y0, y1), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(y0, y1);
    }
}

} // namespace chronon3d::blend2d_bridge
