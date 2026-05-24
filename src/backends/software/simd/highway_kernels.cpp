// highway_kernels.cpp - SIMD kernels using Google Highway

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_kernels.cpp"
#include <hwy/foreach_target.h>  // IWYU pragma: keep

#include <hwy/highway.h>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <cmath>
#include <array>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

inline const float* get_linear_to_srgb_lut_f32() {
    static const auto lut = []() {
        std::array<float, 4096> arr;
        for (size_t i = 0; i < 4096; ++i) {
            const float linear = static_cast<float>(i) / 4095.0f;
            const float srgb = (linear <= 0.0031308f)
                ? (linear * 12.92f)
                : (1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
            arr[i] = std::clamp(srgb, 0.0f, 1.0f);
        }
        return arr;
    }();
    return lut.data();
}

// ── Composite Normal Premultiplied (SRC_OVER) ───────────────────────────────

HWY_ATTR void composite_normal_premul_impl(float* HWY_RESTRICT dst_ptr,
                                           const float* HWY_RESTRICT src_ptr,
                                           int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        float* d = dst_ptr + i * 4;
        const float* s = src_ptr + i * 4;
        float inv_sa = 1.0f - s[3];
        d[0] = s[0] + d[0] * inv_sa;
        d[1] = s[1] + d[1] * inv_sa;
        d[2] = s[2] + d[2] * inv_sa;
        d[3] = s[3] + d[3] * inv_sa;
    }
}

// ── Rasterize Rectangle ─────────────────────────────────────────────────────

HWY_ATTR void rasterize_rect_simd_impl(
    float* HWY_RESTRICT row_ptr,
    const float* HWY_RESTRICT lp_h_start,
    const float* HWY_RESTRICT col0,
    int pixel_count,
    float rect_w, float rect_h,
    float spread,
    const float* HWY_RESTRICT color_ptr
) {
    const float r = color_ptr[0];
    const float g = color_ptr[1];
    const float b = color_ptr[2];
    const float a = color_ptr[3];
    const float inv_spread = 1.0f / std::max(0.001f, spread);

    for (int x = 0; x < pixel_count; ++x) {
        float px = lp_h_start[0] + col0[0] * x;
        float py = lp_h_start[1] + col0[1] * x;
        float pz = lp_h_start[2] + col0[2] * x;

        if (std::abs(pz) < 1e-7f) continue;
        float lx = px / pz;
        float ly = py / pz;

        float dx = std::max(0.0f, std::abs(lx - rect_w * 0.5f) - rect_w * 0.5f);
        float dy = std::max(0.0f, std::abs(ly - rect_h * 0.5f) - rect_h * 0.5f);
        float dist = std::sqrt(dx * dx + dy * dy);

        float mask = std::clamp(1.0f - dist * inv_spread, 0.0f, 1.0f);
        if (mask <= 0.001f) continue;

        float sa = a * mask;
        float inv_sa = 1.0f - sa;
        float* d = row_ptr + x * 4;
        d[0] = r * sa + d[0] * inv_sa;
        d[1] = g * sa + d[1] * inv_sa;
        d[2] = b * sa + d[2] * inv_sa;
        d[3] = sa + d[3] * inv_sa;
    }
}

// ── Premultiply Alpha RGBA8 ─────────────────────────────────────────────────

HWY_ATTR void premultiply_alpha_rgba8_impl(uint32_t* HWY_RESTRICT dst,
                                           const uint8_t* HWY_RESTRICT src,
                                           int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        const uint8_t r = src[i * 4 + 0];
        const uint8_t g = src[i * 4 + 1];
        const uint8_t b = src[i * 4 + 2];
        const uint8_t a = src[i * 4 + 3];
        const uint32_t pr = (static_cast<uint32_t>(r) * a + 127) / 255;
        const uint32_t pg = (static_cast<uint32_t>(g) * a + 127) / 255;
        const uint32_t pb = (static_cast<uint32_t>(b) * a + 127) / 255;
        dst[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
    }
}

// ── F32 RGBA to U8 RGBA Conversion ──────────────────────────────────────────

HWY_ATTR void convert_f32_rgba_to_u8_rgba_impl(uint8_t* HWY_RESTRICT dst_ptr,
                                               const float* HWY_RESTRICT src_ptr,
                                               int pixel_count) {
    const hn::ScalableTag<float> df;
    const hn::ScalableTag<int32_t> di32;
    const hn::ScalableTag<int16_t> di16;
    const hn::ScalableTag<uint8_t> du8;

    const auto zero = hn::Set(df, 0.0f);
    const auto one = hn::Set(df, 1.0f);
    const auto scale = hn::Set(df, 255.0f);

    const int lanes = hn::Lanes(df);
    const int total_floats = pixel_count * 4;
    const int vectorized_floats = (total_floats / (lanes * 4)) * (lanes * 4);

    for (int i = 0; i < vectorized_floats; i += lanes * 4) {
        auto f0 = hn::LoadU(df, src_ptr + i + lanes * 0);
        auto f1 = hn::LoadU(df, src_ptr + i + lanes * 1);
        auto f2 = hn::LoadU(df, src_ptr + i + lanes * 2);
        auto f3 = hn::LoadU(df, src_ptr + i + lanes * 3);

        auto i0 = hn::ConvertTo(di32, hn::Round(hn::Mul(hn::Clamp(f0, zero, one), scale)));
        auto i1 = hn::ConvertTo(di32, hn::Round(hn::Mul(hn::Clamp(f1, zero, one), scale)));
        auto i2 = hn::ConvertTo(di32, hn::Round(hn::Mul(hn::Clamp(f2, zero, one), scale)));
        auto i3 = hn::ConvertTo(di32, hn::Round(hn::Mul(hn::Clamp(f3, zero, one), scale)));

        auto s01 = hn::OrderedDemote2To(di16, i0, i1);
        auto s23 = hn::OrderedDemote2To(di16, i2, i3);
        auto b0123 = hn::OrderedDemote2To(du8, s01, s23);

        hn::StoreU(b0123, du8, dst_ptr + (i / 4));
    }

    for (int i = vectorized_floats / 4; i < pixel_count; ++i) {
        for (int c = 0; c < 4; ++c) {
            float v = std::clamp(src_ptr[i * 4 + c], 0.0f, 1.0f);
            dst_ptr[i * 4 + c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
        }
    }
}

// ── F32 RGBA to YUV Conversion (Row-aware) ──────────────────────────────────

HWY_ATTR void convert_f32_rgba_to_yuv420p_simd_rows_impl(
    uint8_t* HWY_RESTRICT y_ptr,
    uint8_t* HWY_RESTRICT u_ptr,
    uint8_t* HWY_RESTRICT v_ptr,
    const float* HWY_RESTRICT src_ptr,
    int width, int height,
    int src_stride,
    int y_start, int y_end,
    bool apply_gamma) {

    const hn::ScalableTag<float> df;
    const hn::ScalableTag<int32_t> di32;
    const hn::ScalableTag<int16_t> di16;
    const hn::ScalableTag<uint8_t> du8;

    const auto zero = hn::Set(df, 0.0f);
    const auto one = hn::Set(df, 1.0f);
    const auto scale_4095 = hn::Set(df, 4095.0f);
    const float* lut_f32_ptr = get_linear_to_srgb_lut_f32();

    auto apply_srgb_gamma = [&](auto v) {
        auto idx = hn::ConvertTo(di32, hn::Round(hn::Mul(hn::Clamp(v, zero, one), scale_4095)));
        return hn::GatherIndex(df, lut_f32_ptr, idx);
    };

    // BT.709 coefficients
    const auto kRY = hn::Set(df, 0.2126f);
    const auto kGY = hn::Set(df, 0.7152f);
    const auto kBY = hn::Set(df, 0.0722f);

    const auto y_scale = hn::Set(df, 219.0f);
    const auto y_offset = hn::Set(df, 16.5f);

    const int lanes = hn::Lanes(df);
    const int vectorized_pixels_per_row = (width / (lanes * 4)) * (lanes * 4);

    for (int y = y_start; y < y_end; ++y) {
        uint8_t* dst_y = y_ptr + static_cast<size_t>(y) * width;
        const float* src_row = src_ptr + static_cast<size_t>(y) * src_stride * 4;

        // 1. Luma (Y)
        for (int x = 0; x < vectorized_pixels_per_row; x += lanes * 4) {
            hn::Vec<decltype(df)> r0, g0, b0, a0, r1, g1, b1, a1, r2, g2, b2, a2, r3, g3, b3, a3;
            hn::LoadInterleaved4(df, src_row + (x + lanes * 0) * 4, r0, g0, b0, a0);
            hn::LoadInterleaved4(df, src_row + (x + lanes * 1) * 4, r1, g1, b1, a1);
            hn::LoadInterleaved4(df, src_row + (x + lanes * 2) * 4, r2, g2, b2, a2);
            hn::LoadInterleaved4(df, src_row + (x + lanes * 3) * 4, r3, g3, b3, a3);

            auto compute_y = [&](auto r, auto g, auto b) {
                auto r_gamma = apply_gamma ? apply_srgb_gamma(r) : hn::Clamp(r, zero, one);
                auto g_gamma = apply_gamma ? apply_srgb_gamma(g) : hn::Clamp(g, zero, one);
                auto b_gamma = apply_gamma ? apply_srgb_gamma(b) : hn::Clamp(b, zero, one);
                auto yy = hn::MulAdd(r_gamma, kRY, 
                          hn::MulAdd(g_gamma, kGY, 
                          hn::Mul(b_gamma, kBY)));
                return hn::ConvertTo(di32, hn::Round(hn::MulAdd(yy, y_scale, y_offset)));
            };

            auto y0 = compute_y(r0, g0, b0);
            auto y1 = compute_y(r1, g1, b1);
            auto y2 = compute_y(r2, g2, b2);
            auto y3 = compute_y(r3, g3, b3);

            auto s01 = hn::OrderedDemote2To(di16, y0, y1);
            auto s23 = hn::OrderedDemote2To(di16, y2, y3);
            auto b0123 = hn::OrderedDemote2To(du8, s01, s23);

            hn::StoreU(b0123, du8, dst_y + x);
        }

        // Tail Luma
        for (int x = vectorized_pixels_per_row; x < width; ++x) {
            const float* p = src_row + x * 4;
            float r = std::clamp(p[0], 0.0f, 1.0f);
            float g = std::clamp(p[1], 0.0f, 1.0f);
            float b = std::clamp(p[2], 0.0f, 1.0f);
            if (apply_gamma) {
                r = (r <= 0.0031308f) ? (r * 12.92f) : (1.055f * std::pow(r, 1.0f / 2.4f) - 0.055f);
                g = (g <= 0.0031308f) ? (g * 12.92f) : (1.055f * std::pow(g, 1.0f / 2.4f) - 0.055f);
                b = (b <= 0.0031308f) ? (b * 12.92f) : (1.055f * std::pow(b, 1.0f / 2.4f) - 0.055f);
            }
            float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            dst_y[x] = static_cast<uint8_t>(std::clamp(std::round(yy * 219.0f + 16.5f), 0.0f, 255.0f));
        }
    }

    // 2. Chroma (UV) - Subsampled 2x2.
    // UV rows must be written only for even source rows, otherwise parallel
    // row ranges can alias and overwrite the same chroma line twice.
    if (u_ptr && v_ptr) {
        const int uv_row_begin = std::max((y_start + 1) & ~1, 0);
        const int uv_row_end = std::min(y_end, height) & ~1;
        const int uv_width = width / 2;

        for (int y = uv_row_begin; y < uv_row_end; y += 2) {
            const float* row0 = src_ptr + static_cast<size_t>(y) * src_stride * 4;
            const float* row1 = row0 + static_cast<size_t>(src_stride) * 4;
            uint8_t* dst_u = u_ptr + (static_cast<size_t>(y) / 2) * uv_width;
            uint8_t* dst_v = v_ptr + (static_cast<size_t>(y) / 2) * uv_width;

            for (int bx = 0; bx < uv_width; ++bx) {
                const int x = bx * 2;
                const float* p00 = row0 + static_cast<size_t>(x) * 4;
                const float* p01 = p00 + 4;
                const float* p10 = row1 + static_cast<size_t>(x) * 4;
                const float* p11 = p10 + 4;

                float r = 0.25f * (
                    std::clamp(p00[0], 0.0f, 1.0f) +
                    std::clamp(p01[0], 0.0f, 1.0f) +
                    std::clamp(p10[0], 0.0f, 1.0f) +
                    std::clamp(p11[0], 0.0f, 1.0f));
                float g = 0.25f * (
                    std::clamp(p00[1], 0.0f, 1.0f) +
                    std::clamp(p01[1], 0.0f, 1.0f) +
                    std::clamp(p10[1], 0.0f, 1.0f) +
                    std::clamp(p11[1], 0.0f, 1.0f));
                float b = 0.25f * (
                    std::clamp(p00[2], 0.0f, 1.0f) +
                    std::clamp(p01[2], 0.0f, 1.0f) +
                    std::clamp(p10[2], 0.0f, 1.0f) +
                    std::clamp(p11[2], 0.0f, 1.0f));

                if (apply_gamma) {
                    r = (r <= 0.0031308f) ? (r * 12.92f) : (1.055f * std::pow(r, 1.0f / 2.4f) - 0.055f);
                    g = (g <= 0.0031308f) ? (g * 12.92f) : (1.055f * std::pow(g, 1.0f / 2.4f) - 0.055f);
                    b = (b <= 0.0031308f) ? (b * 12.92f) : (1.055f * std::pow(b, 1.0f / 2.4f) - 0.055f);
                }

                const float u = -0.1146f * r - 0.3854f * g + 0.5000f * b;
                const float v =  0.5000f * r - 0.4542f * g - 0.0458f * b;

                dst_u[bx] = static_cast<uint8_t>(std::clamp(std::round(u * 224.0f + 128.5f), 0.0f, 255.0f));
                dst_v[bx] = static_cast<uint8_t>(std::clamp(std::round(v * 224.0f + 128.5f), 0.0f, 255.0f));
            }
        }
    }
}

HWY_ATTR void convert_f32_rgba_to_nv12_simd_rows_impl(
    uint8_t* HWY_RESTRICT y_ptr,
    uint8_t* HWY_RESTRICT uv_ptr,
    const float* HWY_RESTRICT src_ptr,
    int width, int height,
    int src_stride,
    int y_start, int y_end,
    bool apply_gamma) {

    // Reuse Luma Y logic
    convert_f32_rgba_to_yuv420p_simd_rows_impl(y_ptr, nullptr, nullptr, src_ptr, width, height, src_stride, y_start, y_end, apply_gamma);
    
    // UV Interleaved
    const int uv_row_begin = std::max((y_start + 1) & ~1, 0);
    const int uv_row_end = std::min(y_end, height) & ~1;
    const int uv_width = width / 2;

    for (int y = uv_row_begin; y < uv_row_end; y += 2) {
        const float* row0 = src_ptr + static_cast<size_t>(y) * src_stride * 4;
        const float* row1 = row0 + static_cast<size_t>(src_stride) * 4;
        uint8_t* dst_uv = uv_ptr + (static_cast<size_t>(y) / 2) * uv_width * 2;

        for (int bx = 0; bx < uv_width; ++bx) {
            const int x = bx * 2;
            const float* p00 = row0 + static_cast<size_t>(x) * 4;
            const float* p01 = p00 + 4;
            const float* p10 = row1 + static_cast<size_t>(x) * 4;
            const float* p11 = p10 + 4;

            float r = 0.25f * (
                std::clamp(p00[0], 0.0f, 1.0f) +
                std::clamp(p01[0], 0.0f, 1.0f) +
                std::clamp(p10[0], 0.0f, 1.0f) +
                std::clamp(p11[0], 0.0f, 1.0f));
            float g = 0.25f * (
                std::clamp(p00[1], 0.0f, 1.0f) +
                std::clamp(p01[1], 0.0f, 1.0f) +
                std::clamp(p10[1], 0.0f, 1.0f) +
                std::clamp(p11[1], 0.0f, 1.0f));
            float b = 0.25f * (
                std::clamp(p00[2], 0.0f, 1.0f) +
                std::clamp(p01[2], 0.0f, 1.0f) +
                std::clamp(p10[2], 0.0f, 1.0f) +
                std::clamp(p11[2], 0.0f, 1.0f));

            if (apply_gamma) {
                r = (r <= 0.0031308f) ? (r * 12.92f) : (1.055f * std::pow(r, 1.0f / 2.4f) - 0.055f);
                g = (g <= 0.0031308f) ? (g * 12.92f) : (1.055f * std::pow(g, 1.0f / 2.4f) - 0.055f);
                b = (b <= 0.0031308f) ? (b * 12.92f) : (1.055f * std::pow(b, 1.0f / 2.4f) - 0.055f);
            }

            const float u = -0.1146f * r - 0.3854f * g + 0.5000f * b;
            const float v =  0.5000f * r - 0.4542f * g - 0.0458f * b;

            const size_t uv_idx = static_cast<size_t>(bx) * 2;
            dst_uv[uv_idx + 0] = static_cast<uint8_t>(std::clamp(std::round(u * 224.0f + 128.5f), 0.0f, 255.0f));
            dst_uv[uv_idx + 1] = static_cast<uint8_t>(std::clamp(std::round(v * 224.0f + 128.5f), 0.0f, 255.0f));
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {

HWY_EXPORT(composite_normal_premul_impl);
HWY_EXPORT(rasterize_rect_simd_impl);
HWY_EXPORT(premultiply_alpha_rgba8_impl);
HWY_EXPORT(convert_f32_rgba_to_u8_rgba_impl);
HWY_EXPORT(convert_f32_rgba_to_yuv420p_simd_rows_impl);
HWY_EXPORT(convert_f32_rgba_to_nv12_simd_rows_impl);

void composite_normal_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void rasterize_rect_simd(Color* __restrict__ row, const float* __restrict__ lp_h_start, const float* __restrict__ col0, int pixel_count, float rect_w, float rect_h, float spread, const Color& color) {
    HWY_DYNAMIC_DISPATCH(rasterize_rect_simd_impl)(reinterpret_cast<float*>(row), lp_h_start, col0, pixel_count, rect_w, rect_h, spread, reinterpret_cast<const float*>(&color));
}

void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(premultiply_alpha_rgba8_impl)(dst, src, pixel_count);
}

void convert_f32_rgba_to_u8_rgba(uint8_t* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(convert_f32_rgba_to_u8_rgba_impl)(dst, reinterpret_cast<const float*>(src), pixel_count);
}

void convert_f32_rgba_to_yuv420p_simd_rows(uint8_t* __restrict__ y_ptr, uint8_t* __restrict__ u_ptr, uint8_t* __restrict__ v_ptr, const Color* __restrict__ src, int width, int height, int src_stride, int y_start, int y_end, bool apply_gamma) {
    HWY_DYNAMIC_DISPATCH(convert_f32_rgba_to_yuv420p_simd_rows_impl)(y_ptr, u_ptr, v_ptr, reinterpret_cast<const float*>(src), width, height, src_stride, y_start, y_end, apply_gamma);
}

void convert_f32_rgba_to_nv12_simd_rows(uint8_t* __restrict__ y_ptr, uint8_t* __restrict__ uv_ptr, const Color* __restrict__ src, int width, int height, int src_stride, int y_start, int y_end, bool apply_gamma) {
    HWY_DYNAMIC_DISPATCH(convert_f32_rgba_to_nv12_simd_rows_impl)(y_ptr, uv_ptr, reinterpret_cast<const float*>(src), width, height, src_stride, y_start, y_end, apply_gamma);
}

void clear_framebuffer(Color* data, int pixel_count, const Color& color) {
    std::fill(data, data + pixel_count, color);
}

}  // namespace chronon3d::simd
#endif
