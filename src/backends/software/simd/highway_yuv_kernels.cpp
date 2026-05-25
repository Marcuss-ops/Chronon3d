#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_yuv_kernels.cpp"
#include <hwy/foreach_target.h>

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

HWY_ATTR void convert_uv_pair_rows_vectorized_impl(
    uint8_t* dst_u,
    uint8_t* dst_v,
    uint8_t* dst_uv,
    const float* src_ptr,
    int width,
    int height,
    int src_stride,
    int y_start,
    int y_end,
    bool apply_gamma,
    bool planar_output) {

    const hn::ScalableTag<float> df;
    const hn::ScalableTag<int32_t> di32;

    const auto zero   = hn::Set(df, 0.0f);
    const auto one    = hn::Set(df, 1.0f);
    const auto s4095  = hn::Set(df, 4095.0f);
    const auto uvscl  = hn::Set(df, 224.0f);
    const auto uvoff  = hn::Set(df, 128.5f);
    const float* lut  = get_linear_to_srgb_lut_f32();

    const auto kU_R = hn::Set(df, -0.1146f);
    const auto kU_G = hn::Set(df, -0.3854f);
    const auto kU_B = hn::Set(df,  0.5000f);
    const auto kV_R = hn::Set(df,  0.5000f);
    const auto kV_G = hn::Set(df, -0.4542f);
    const auto kV_B = hn::Set(df, -0.0458f);

    auto srgb = [&](auto v) {
        auto idx = hn::ConvertTo(di32, hn::Round(hn::Mul(hn::Clamp(v, zero, one), s4095)));
        return hn::GatherIndex(df, lut, idx);
    };

    const int uv_width   = width / 2;
    const int lanes      = hn::Lanes(df);
    const int vec_blocks = (uv_width / lanes) * lanes;

    const int uv_row_begin = std::max((y_start + 1) & ~1, 0);
    const int uv_row_end   = std::min(y_end, height) & ~1;

    alignas(64) float r_buf[16];
    alignas(64) float g_buf[16];
    alignas(64) float b_buf[16];
    alignas(64) float u_buf[16];
    alignas(64) float v_buf[16];

    for (int y = uv_row_begin; y < uv_row_end; y += 2) {
        const float* row0 = src_ptr + static_cast<size_t>(y) * src_stride * 4;
        const float* row1 = row0    + static_cast<size_t>(src_stride) * 4;

        uint8_t* out_u  = planar_output ? (dst_u  + (static_cast<size_t>(y) / 2) * uv_width) : nullptr;
        uint8_t* out_v  = planar_output ? (dst_v  + (static_cast<size_t>(y) / 2) * uv_width) : nullptr;
        uint8_t* out_uv = planar_output ? nullptr : (dst_uv + (static_cast<size_t>(y) / 2) * uv_width * 2);

        int bx = 0;

        for (; bx < vec_blocks; bx += lanes) {
            for (int i = 0; i < lanes; ++i) {
                const int px = (bx + i) * 2;
                const float* p00 = row0 + static_cast<size_t>(px) * 4;
                const float* p01 = p00 + 4;
                const float* p10 = row1 + static_cast<size_t>(px) * 4;
                const float* p11 = p10 + 4;

                r_buf[i] = 0.25f * (
                    std::clamp(p00[0], 0.0f, 1.0f) +
                    std::clamp(p01[0], 0.0f, 1.0f) +
                    std::clamp(p10[0], 0.0f, 1.0f) +
                    std::clamp(p11[0], 0.0f, 1.0f));
                g_buf[i] = 0.25f * (
                    std::clamp(p00[1], 0.0f, 1.0f) +
                    std::clamp(p01[1], 0.0f, 1.0f) +
                    std::clamp(p10[1], 0.0f, 1.0f) +
                    std::clamp(p11[1], 0.0f, 1.0f));
                b_buf[i] = 0.25f * (
                    std::clamp(p00[2], 0.0f, 1.0f) +
                    std::clamp(p01[2], 0.0f, 1.0f) +
                    std::clamp(p10[2], 0.0f, 1.0f) +
                    std::clamp(p11[2], 0.0f, 1.0f));
            }

            auto rv = hn::LoadU(df, r_buf);
            auto gv = hn::LoadU(df, g_buf);
            auto bv = hn::LoadU(df, b_buf);

            if (apply_gamma) {
                rv = srgb(rv);
                gv = srgb(gv);
                bv = srgb(bv);
            }

            auto uv = hn::MulAdd(rv, kU_R, hn::MulAdd(gv, kU_G, hn::Mul(bv, kU_B)));
            auto vv = hn::MulAdd(rv, kV_R, hn::MulAdd(gv, kV_G, hn::Mul(bv, kV_B)));

            uv = hn::MulAdd(uv, uvscl, uvoff);
            vv = hn::MulAdd(vv, uvscl, uvoff);

            hn::StoreU(uv, df, u_buf);
            hn::StoreU(vv, df, v_buf);
            for (int i = 0; i < lanes; ++i) {
                int u_byte = static_cast<int>(std::round(u_buf[i]));
                int v_byte = static_cast<int>(std::round(v_buf[i]));
                if (planar_output) {
                    out_u[bx + i] = static_cast<uint8_t>(std::clamp(u_byte, 0, 255));
                    out_v[bx + i] = static_cast<uint8_t>(std::clamp(v_byte, 0, 255));
                } else {
                    out_uv[(bx + i) * 2 + 0] = static_cast<uint8_t>(std::clamp(u_byte, 0, 255));
                    out_uv[(bx + i) * 2 + 1] = static_cast<uint8_t>(std::clamp(v_byte, 0, 255));
                }
            }
        }

        for (; bx < uv_width; ++bx) {
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

            float u = -0.1146f * r - 0.3854f * g + 0.5000f * b;
            float v =  0.5000f * r - 0.4542f * g - 0.0458f * b;

            if (planar_output) {
                out_u[bx] = static_cast<uint8_t>(std::clamp(std::round(u * 224.0f + 128.5f), 0.0f, 255.0f));
                out_v[bx] = static_cast<uint8_t>(std::clamp(std::round(v * 224.0f + 128.5f), 0.0f, 255.0f));
            } else {
                out_uv[bx * 2 + 0] = static_cast<uint8_t>(std::clamp(std::round(u * 224.0f + 128.5f), 0.0f, 255.0f));
                out_uv[bx * 2 + 1] = static_cast<uint8_t>(std::clamp(std::round(v * 224.0f + 128.5f), 0.0f, 255.0f));
            }
        }
    }
}

HWY_ATTR void convert_uv_pair_rows_scalar_impl(
    uint8_t* dst_u,
    uint8_t* dst_v,
    uint8_t* dst_uv,
    const float* src_ptr,
    int width,
    int height,
    int src_stride,
    int y_start,
    int y_end,
    bool apply_gamma,
    bool planar_output) {
    const int uv_row_begin = std::max((y_start + 1) & ~1, 0);
    const int uv_row_end = std::min(y_end, height) & ~1;
    const int uv_width = width / 2;

    for (int y = uv_row_begin; y < uv_row_end; y += 2) {
        const float* row0 = src_ptr + static_cast<size_t>(y) * src_stride * 4;
        const float* row1 = row0 + static_cast<size_t>(src_stride) * 4;
        uint8_t* out_u = planar_output ? (dst_u + (static_cast<size_t>(y) / 2) * uv_width) : nullptr;
        uint8_t* out_v = planar_output ? (dst_v + (static_cast<size_t>(y) / 2) * uv_width) : nullptr;
        uint8_t* out_uv = planar_output ? nullptr : (dst_uv + (static_cast<size_t>(y) / 2) * uv_width * 2);

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

            const uint8_t u8 = static_cast<uint8_t>(std::clamp(std::round(u * 224.0f + 128.5f), 0.0f, 255.0f));
            const uint8_t v8 = static_cast<uint8_t>(std::clamp(std::round(v * 224.0f + 128.5f), 0.0f, 255.0f));

            if (planar_output) {
                out_u[bx] = u8;
                out_v[bx] = v8;
            } else {
                out_uv[bx * 2 + 0] = u8;
                out_uv[bx * 2 + 1] = v8;
            }
        }
    }
}

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

    if (u_ptr && v_ptr) {
        convert_uv_pair_rows_vectorized_impl(
            u_ptr, v_ptr, nullptr,
            src_ptr,
            width, height, src_stride,
            y_start, y_end,
            apply_gamma,
            true);
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

    convert_f32_rgba_to_yuv420p_simd_rows_impl(y_ptr, nullptr, nullptr, src_ptr, width, height, src_stride, y_start, y_end, apply_gamma);
    
    convert_uv_pair_rows_vectorized_impl(
        nullptr, nullptr, uv_ptr,
        src_ptr,
        width, height, src_stride,
        y_start, y_end,
        apply_gamma,
        false);
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {

HWY_EXPORT(convert_f32_rgba_to_yuv420p_simd_rows_impl);
HWY_EXPORT(convert_f32_rgba_to_nv12_simd_rows_impl);

void convert_f32_rgba_to_yuv420p_simd_rows(uint8_t* __restrict__ y_ptr, uint8_t* __restrict__ u_ptr, uint8_t* __restrict__ v_ptr, const Color* __restrict__ src, int width, int height, int src_stride, int y_start, int y_end, bool apply_gamma) {
    HWY_DYNAMIC_DISPATCH(convert_f32_rgba_to_yuv420p_simd_rows_impl)(y_ptr, u_ptr, v_ptr, reinterpret_cast<const float*>(src), width, height, src_stride, y_start, y_end, apply_gamma);
}

void convert_f32_rgba_to_nv12_simd_rows(uint8_t* __restrict__ y_ptr, uint8_t* __restrict__ uv_ptr, const Color* __restrict__ src, int width, int height, int src_stride, int y_start, int y_end, bool apply_gamma) {
    HWY_DYNAMIC_DISPATCH(convert_f32_rgba_to_nv12_simd_rows_impl)(y_ptr, uv_ptr, reinterpret_cast<const float*>(src), width, height, src_stride, y_start, y_end, apply_gamma);
}

}  // namespace chronon3d::simd
#endif
