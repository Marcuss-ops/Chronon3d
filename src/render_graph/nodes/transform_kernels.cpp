#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <cstring>

#include <hwy/highway.h>
#include <spdlog/spdlog.h>

#include "sampling_utils.hpp"

namespace chronon3d::graph::detail {
namespace hn = hwy::HWY_NAMESPACE;

// ── Highway SIMD bilinear sample (1 pixel, FixedTag<4>) ────────────
// Drop-in replacement for sample_bilinear() with explicit SIMD lerps.
// On SSE4/NEON/AVX2 this replaces 12 scalar FMA instructions with 3 SIMD FMA.
HWY_INLINE Color bilinear_sample_hwy(
    const Color* src, int32_t src_w, int32_t src_h, int32_t stride,
    float sx, float sy)
{
    const float u = sx - 0.5f;
    const float v = sy - 0.5f;
    const int32_t x0 = static_cast<int32_t>(u >= 0.0f ? u : std::floor(u));
    const int32_t y0 = static_cast<int32_t>(v >= 0.0f ? v : std::floor(v));
    const int32_t x1 = x0 + 1;
    const int32_t y1 = y0 + 1;
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    const hn::FixedTag<float, 4> d4;

    Color c00_s, c10_s, c01_s, c11_s;
    const Color* c00_p = nullptr;
    const Color* c10_p = nullptr;
    const Color* c01_p = nullptr;
    const Color* c11_p = nullptr;

    // Fast path: all 4 samples guaranteed in-bounds.
    if (x1 < src_w && y1 < src_h && x0 >= 0 && y0 >= 0) {
        const Color* base = src + static_cast<size_t>(y0) * stride;
        const Color* base_next = src + static_cast<size_t>(y1) * stride;
        c00_p = base + x0;
        c10_p = base + x1;
        c01_p = base_next + x0;
        c11_p = base_next + x1;
    } else {
        c00_s = get_clamped_pixel(src, src_w, src_h, stride, x0, y0);
        c10_s = get_clamped_pixel(src, src_w, src_h, stride, x1, y0);
        c01_s = get_clamped_pixel(src, src_w, src_h, stride, x0, y1);
        c11_s = get_clamped_pixel(src, src_w, src_h, stride, x1, y1);
        c00_p = &c00_s; c10_p = &c10_s; c01_p = &c01_s; c11_p = &c11_s;
    }

    auto v_c00 = hn::LoadU(d4, &c00_p->r);
    auto v_c10 = hn::LoadU(d4, &c10_p->r);
    auto v_c01 = hn::LoadU(d4, &c01_p->r);
    auto v_c11 = hn::LoadU(d4, &c11_p->r);

    auto v_tx = hn::Set(d4, tx);
    auto v_ty = hn::Set(d4, ty);

    // 3 bilinear lerps: top=c00+(c10-c00)*tx, bot=c01+(c11-c01)*tx, res=top+(bot-top)*ty
    auto v_top = hn::MulAdd(hn::Sub(v_c10, v_c00), v_tx, v_c00);
    auto v_bot = hn::MulAdd(hn::Sub(v_c11, v_c01), v_tx, v_c01);
    auto v_res = hn::MulAdd(hn::Sub(v_bot, v_top), v_ty, v_top);

    Color result;
    hn::StoreU(v_res, d4, &result.r);
    return result;
}

// ── Highway SIMD opacity application ───────────────────────────────
HWY_INLINE Color apply_opacity_hwy(Color c, float opacity) {
    if (opacity >= 0.999f) return c;
    const hn::FixedTag<float, 4> d4;
    auto v = hn::LoadU(d4, &c.r);
    auto v_res = hn::Mul(v, hn::Set(d4, opacity));
    Color result;
    hn::StoreU(v_res, d4, &result.r);
    return result;
}

// ── 2-pixel AVX2 bilinear sampler ──────────────────────────────────
// Processes two horizontally-adjacent output pixels with a single SIMD pass.
// Uses 8-lane ScalableTag + Combine to interleave pixel A (lanes 0-3) and B (4-7).
#if HWY_TARGET == HWY_AVX2
HWY_INLINE void bilinear_sample_2px_avx2(
    const Color* src, int32_t src_w, int32_t src_h, int32_t stride,
    float sx_a, float sy_a, float sx_b, float sy_b,
    float opacity, Color* HWY_RESTRICT dst_a, Color* HWY_RESTRICT dst_b)
{
    const hn::ScalableTag<float> d;       // 8 lanes
    const hn::FixedTag<float, 4> d4;      // 4 lanes per pixel

    // ── Compute bilinear coords for pixel A ────────────────────────
    const float ua = sx_a - 0.5f;
    const float va = sy_a - 0.5f;
    const int32_t x0a = static_cast<int32_t>(ua >= 0.0f ? ua : std::floor(ua));
    const int32_t y0a = static_cast<int32_t>(va >= 0.0f ? va : std::floor(va));
    const float txa = ua - static_cast<float>(x0a);
    const float tya = va - static_cast<float>(y0a);
    const int32_t x1a = x0a + 1, y1a = y0a + 1;

    // ── Compute bilinear coords for pixel B ────────────────────────
    const float ub = sx_b - 0.5f;
    const float vb = sy_b - 0.5f;
    const int32_t x0b = static_cast<int32_t>(ub >= 0.0f ? ub : std::floor(ub));
    const int32_t y0b = static_cast<int32_t>(vb >= 0.0f ? vb : std::floor(vb));
    const float txb = ub - static_cast<float>(x0b);
    const float tyb = vb - static_cast<float>(y0b);
    const int32_t x1b = x0b + 1, y1b = y0b + 1;

    // ── Load 4 corners for each pixel ──────────────────────────────
    Color c00a_s, c10a_s, c01a_s, c11a_s;
    Color c00b_s, c10b_s, c01b_s, c11b_s;
    const Color *p00a, *p10a, *p01a, *p11a;
    const Color *p00b, *p10b, *p01b, *p11b;

    if (x1a < src_w && y1a < src_h && x0a >= 0 && y0a >= 0) {
        const Color* base_a = src + static_cast<size_t>(y0a) * stride;
        const Color* base_next_a = src + static_cast<size_t>(y1a) * stride;
        p00a = base_a + x0a;  p10a = base_a + x1a;
        p01a = base_next_a + x0a;  p11a = base_next_a + x1a;
    } else {
        c00a_s = get_clamped_pixel(src, src_w, src_h, stride, x0a, y0a);
        c10a_s = get_clamped_pixel(src, src_w, src_h, stride, x1a, y0a);
        c01a_s = get_clamped_pixel(src, src_w, src_h, stride, x0a, y1a);
        c11a_s = get_clamped_pixel(src, src_w, src_h, stride, x1a, y1a);
        p00a = &c00a_s; p10a = &c10a_s; p01a = &c01a_s; p11a = &c11a_s;
    }

    if (x1b < src_w && y1b < src_h && x0b >= 0 && y0b >= 0) {
        const Color* base_b = src + static_cast<size_t>(y0b) * stride;
        const Color* base_next_b = src + static_cast<size_t>(y1b) * stride;
        p00b = base_b + x0b;  p10b = base_b + x1b;
        p01b = base_next_b + x0b;  p11b = base_next_b + x1b;
    } else {
        c00b_s = get_clamped_pixel(src, src_w, src_h, stride, x0b, y0b);
        c10b_s = get_clamped_pixel(src, src_w, src_h, stride, x1b, y0b);
        c01b_s = get_clamped_pixel(src, src_w, src_h, stride, x0b, y1b);
        c11b_s = get_clamped_pixel(src, src_w, src_h, stride, x1b, y1b);
        p00b = &c00b_s; p10b = &c10b_s; p01b = &c01b_s; p11b = &c11b_s;
    }

    // ── Pack into 8-lane vectors (A=lanes 0-3, B=lanes 4-7) ───────
    auto v_c00 = hn::Combine(d, hn::LoadU(d4, &p00b->r), hn::LoadU(d4, &p00a->r));
    auto v_c10 = hn::Combine(d, hn::LoadU(d4, &p10b->r), hn::LoadU(d4, &p10a->r));
    auto v_c01 = hn::Combine(d, hn::LoadU(d4, &p01b->r), hn::LoadU(d4, &p01a->r));
    auto v_c11 = hn::Combine(d, hn::LoadU(d4, &p11b->r), hn::LoadU(d4, &p11a->r));

    auto v_tx = hn::Combine(d, hn::Set(d4, txb), hn::Set(d4, txa));
    auto v_ty = hn::Combine(d, hn::Set(d4, tyb), hn::Set(d4, tya));

    // ── Bilinear lerps in SIMD ─────────────────────────────────────
    auto v_top = hn::MulAdd(hn::Sub(v_c10, v_c00), v_tx, v_c00);
    auto v_bot = hn::MulAdd(hn::Sub(v_c11, v_c01), v_tx, v_c01);
    auto v_res = hn::MulAdd(hn::Sub(v_bot, v_top), v_ty, v_top);

    // ── Apply opacity ──────────────────────────────────────────────
    if (opacity < 0.999f) {
        auto v_op = hn::Combine(d, hn::Set(d4, opacity), hn::Set(d4, opacity));
        v_res = hn::Mul(v_res, v_op);
    }

    // ── Alpha check + store ────────────────────────────────────────
    auto lo = hn::LowerHalf(d4, v_res);
    auto hi = hn::UpperHalf(d4, v_res);
    const float aa = hn::ExtractLane(lo, 3);
    const float ab = hn::ExtractLane(hi, 3);

    if (aa > 0.001f) hn::StoreU(lo, d4, &dst_a->r);
    if (ab > 0.001f) hn::StoreU(hi, d4, &dst_b->r);
}
#endif  // HWY_TARGET == HWY_AVX2

void execute_translate_clamped(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    i32 itx, i32 ity, f32 opacity)
{
    const Color* src_data = input->data();
    const i32 src_w = input->stride();

    auto worker = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            const i32 iy = y - ity;
            if (iy >= static_cast<i32>(y_min_src) && iy < static_cast<i32>(y_max_src)) {
                Color* dst_row = result->pixels_row(y - result->origin_y());
                const i32 dx0 = std::max(x0, static_cast<i32>(x_min_src) + itx);
                const i32 dx1 = std::min(x1, static_cast<i32>(x_max_src) + itx);

                if (dx0 < dx1) {
                    if (opacity >= 0.999f) {
                        Color* dst_ptr = dst_row + (dx0 - result->origin_x());
                        const Color* src_ptr = src_data + (iy * src_w + (dx0 - itx));

                        if (profiling::g_current_counters) {
                            if ((reinterpret_cast<uintptr_t>(dst_ptr) % 32 != 0) || (reinterpret_cast<uintptr_t>(src_ptr) % 32 != 0)) {
                                profiling::g_current_counters->unaligned_memory_copies.fetch_add(1, std::memory_order_relaxed);
                            }
                        }

                        std::memcpy(
                            dst_ptr,
                            src_ptr,
                            static_cast<size_t>(dx1 - dx0) * sizeof(Color)
                        );
                    } else {
                        Color* dst_ptr = dst_row + (dx0 - result->origin_x());
                        const Color* src_ptr = src_data + (iy * src_w + (dx0 - itx));
                        const int count = dx1 - dx0;
                        for (int i = 0; i < count; ++i) {
                            Color src = src_ptr[i];
                            if (src.a > 0.001f) {
                                src.r *= opacity; src.g *= opacity;
                                src.b *= opacity; src.a *= opacity;
                                dst_ptr[i] = src;
                            }
                        }
                    }
                }
            }
        }
    };

    if (y1 - y0 >= 24) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                worker(range.begin(), range.end());
            }
        );
    } else {
        worker(y0, y1);
    }
}

void execute_translate_memcpy(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    i32 itx, i32 ity, f32 opacity)
{
    CHRONON_ZONE_C("transform_fast_translate", trace_category::kRasterize);

    const i32 row_pixels = (x1 - x0);
    const size_t row_bytes = static_cast<size_t>(row_pixels) * sizeof(Color);
    const bool have_unaligned_tracking = profiling::g_current_counters != nullptr;
    const bool full_opacity = std::abs(opacity - 1.0f) < 1e-6f;

    auto worker = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            const i32 src_y = y - ity;
            const i32 src_x = x0 - itx;

            if (src_y >= 0 && src_y < input->height()) {
                const Color* src_ptr = input->pixels_row(src_y) + src_x;
                Color* dst_ptr = result->pixels_row(y - result->origin_y()) + (x0 - result->origin_x());

                if (full_opacity) {
                    if (have_unaligned_tracking) {
                        if ((reinterpret_cast<uintptr_t>(dst_ptr) % 32 != 0) || (reinterpret_cast<uintptr_t>(src_ptr) % 32 != 0)) {
                            profiling::g_current_counters->unaligned_memory_copies.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    std::memcpy(dst_ptr, src_ptr, row_bytes);
                } else {
                    for (i32 x = 0; x < row_pixels; ++x) {
                        Color c = src_ptr[x];
                        c.r *= opacity; c.g *= opacity; c.b *= opacity; c.a *= opacity;
                        dst_ptr[x] = c;
                    }
                }
            }
        }
    };

    if (y1 - y0 >= 24) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                worker(range.begin(), range.end());
            }
        );
    } else {
        worker(y0, y1);
    }
}

void execute_affine_rows(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    f32 opacity, SamplingMode mode,
    Vec3 h_col_start, Vec3 h_step_y,
    f32 inv_z, f32 dsx, f32 dsy,
    i32 row_begin, i32 row_end)
{
    const Color* src_data = input->data();
    const i32 src_w = input->width();
    const i32 src_h = input->height();
    const i32 stride = input->stride();
    const i32 dst_origin_x = result->origin_x();

    if (mode == SamplingMode::Nearest) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            f32 sx = h_row.x * inv_z;
            f32 sy = h_row.y * inv_z;
            for (i32 x = x0; x < x1; ++x) {
                if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                    Color src = sample_nearest(src_data, src_w, src_h, stride, sx, sy);
                    if (src.a > 0.001f) {
                        dst_row[x - dst_origin_x] = apply_opacity_hwy(src, opacity);
                    }
                }
                sx += dsx;
                sy += dsy;
            }
        }
    } else {
        // ── Bilinear: Highway SIMD path ──────────────────────────────
#if HWY_TARGET == HWY_AVX2
        // 2-pixel AVX2 path — processes pairs with 8-lane SIMD.
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            f32 sx = h_row.x * inv_z;
            f32 sy = h_row.y * inv_z;
            const i32 dst_ox = dst_origin_x;
            i32 x = x0;
            // 2-pixel loop
            for (; x + 1 < x1; x += 2) {
                const float sx_b = sx + dsx;
                const float sy_b = sy + dsy;
                const bool in_a = (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src);
                const bool in_b = (sx_b >= x_min_src && sx_b < x_max_src && sy_b >= y_min_src && sy_b < y_max_src);
                if (in_a || in_b) {
                    Color ca = Color::transparent(), cb = Color::transparent();
                    bilinear_sample_2px_avx2(src_data, src_w, src_h, stride,
                        sx, sy, sx_b, sy_b, opacity, &ca, &cb);
                    if (in_a && ca.a > 0.001f) dst_row[x - dst_ox] = ca;
                    if (in_b && cb.a > 0.001f) dst_row[x + 1 - dst_ox] = cb;
                }
                sx = sx_b + dsx;
                sy = sy_b + dsy;
            }
            // 1-pixel remainder
            for (; x < x1; ++x) {
                if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                    Color src = bilinear_sample_hwy(src_data, src_w, src_h, stride, sx, sy);
                    if (src.a > 0.001f) {
                        dst_row[x - dst_ox] = apply_opacity_hwy(src, opacity);
                    }
                }
                sx += dsx;
                sy += dsy;
            }
        }
#else
        // 1-pixel Highway SIMD (universal: SSE4/NEON/AVX2).
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            f32 sx = h_row.x * inv_z;
            f32 sy = h_row.y * inv_z;
            for (i32 x = x0; x < x1; ++x) {
                if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                    Color src = bilinear_sample_hwy(src_data, src_w, src_h, stride, sx, sy);
                    if (src.a > 0.001f) {
                        dst_row[x - dst_origin_x] = apply_opacity_hwy(src, opacity);
                    }
                }
                sx += dsx;
                sy += dsy;
            }
        }
#endif
    }
}

void execute_projective_rows(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    f32 opacity, SamplingMode mode,
    Vec3 h_col_start, Vec3 h_step_x, Vec3 h_step_y,
    i32 row_begin, i32 row_end)
{
    const Color* src_data = input->data();
    const i32 src_w = input->width();
    const i32 src_h = input->height();
    const i32 stride = input->stride();
    const i32 dst_origin_x = result->origin_x();

    if (mode == SamplingMode::Nearest) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            for (i32 x = x0; x < x1; ++x) {
                if (std::abs(h_row.z) > 1e-9f) {
                    const f32 inv_z = 1.0f / h_row.z;
                    const f32 sx = h_row.x * inv_z;
                    const f32 sy = h_row.y * inv_z;
                    if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                        Color src = sample_nearest(src_data, src_w, src_h, stride, sx, sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity_hwy(src, opacity);
                        }
                    }
                }
                h_row += h_step_x;
            }
        }
    } else {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            for (i32 x = x0; x < x1; ++x) {
                if (std::abs(h_row.z) > 1e-9f) {
                    const f32 inv_z = 1.0f / h_row.z;
                    const f32 sx = h_row.x * inv_z;
                    const f32 sy = h_row.y * inv_z;
                    if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                        Color src = bilinear_sample_hwy(src_data, src_w, src_h, stride, sx, sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity_hwy(src, opacity);
                        }
                    }
                }
                h_row += h_step_x;
            }
        }
    }
}

} // namespace chronon3d::graph::detail
