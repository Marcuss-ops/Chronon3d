#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_yuv_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstring>

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
    const auto uvscl  = hn::Set(df, 224.0f);
    const auto uvoff  = hn::Set(df, 128.5f);

    const auto kU_R = hn::Set(df, -0.1146f);
    const auto kU_G = hn::Set(df, -0.3854f);
    const auto kU_B = hn::Set(df,  0.5000f);
    const auto kV_R = hn::Set(df,  0.5000f);
    const auto kV_G = hn::Set(df, -0.4542f);
    const auto kV_B = hn::Set(df, -0.0458f);

    auto srgb_scalar = [](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return (v <= 0.0031308f) ? (v * 12.92f) : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
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

            if (apply_gamma) {
                for (int i = 0; i < lanes; ++i) {
                    r_buf[i] = srgb_scalar(r_buf[i]);
                    g_buf[i] = srgb_scalar(g_buf[i]);
                    b_buf[i] = srgb_scalar(b_buf[i]);
                }
            }

            auto rv = hn::LoadU(df, r_buf);
            auto gv = hn::LoadU(df, g_buf);
            auto bv = hn::LoadU(df, b_buf);

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
    const auto kRY = hn::Set(df, 0.2126f);
    const auto kGY = hn::Set(df, 0.7152f);
    const auto kBY = hn::Set(df, 0.0722f);

    const int lanes = hn::Lanes(df);
    const int vectorized_pixels_per_row = (width / (lanes * 4)) * (lanes * 4);

    auto srgb_scalar = [](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return (v <= 0.0031308f) ? (v * 12.92f) : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
    };

    for (int y = y_start; y < y_end; ++y) {
        uint8_t* dst_y = y_ptr + static_cast<size_t>(y) * width;
        const float* src_row = src_ptr + static_cast<size_t>(y) * src_stride * 4;

        for (int x = 0; x < vectorized_pixels_per_row; x += lanes * 4) {
            hn::Vec<decltype(df)> r0, g0, b0, a0, r1, g1, b1, a1, r2, g2, b2, a2, r3, g3, b3, a3;
            hn::LoadInterleaved4(df, src_row + (x + lanes * 0) * 4, r0, g0, b0, a0);
            hn::LoadInterleaved4(df, src_row + (x + lanes * 1) * 4, r1, g1, b1, a1);
            hn::LoadInterleaved4(df, src_row + (x + lanes * 2) * 4, r2, g2, b2, a2);
            hn::LoadInterleaved4(df, src_row + (x + lanes * 3) * 4, r3, g3, b3, a3);

            alignas(64) float r_buf[16];
            alignas(64) float g_buf[16];
            alignas(64) float b_buf[16];
            alignas(64) uint8_t y_buf[16];

            hn::StoreU(r0, df, r_buf);
            hn::StoreU(g0, df, g_buf);
            hn::StoreU(b0, df, b_buf);
            for (int i = 0; i < lanes; ++i) {
                float r = std::clamp(r_buf[i], 0.0f, 1.0f);
                float g = std::clamp(g_buf[i], 0.0f, 1.0f);
                float b = std::clamp(b_buf[i], 0.0f, 1.0f);
                if (apply_gamma) {
                    r = srgb_scalar(r);
                    g = srgb_scalar(g);
                    b = srgb_scalar(b);
                }
                const float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                y_buf[i] = static_cast<uint8_t>(std::clamp(std::round(yy * 219.0f + 16.5f), 0.0f, 255.0f));
            }
            std::memcpy(dst_y + x, y_buf, static_cast<size_t>(lanes));

            hn::StoreU(r1, df, r_buf);
            hn::StoreU(g1, df, g_buf);
            hn::StoreU(b1, df, b_buf);
            for (int i = 0; i < lanes; ++i) {
                float r = std::clamp(r_buf[i], 0.0f, 1.0f);
                float g = std::clamp(g_buf[i], 0.0f, 1.0f);
                float b = std::clamp(b_buf[i], 0.0f, 1.0f);
                if (apply_gamma) {
                    r = srgb_scalar(r);
                    g = srgb_scalar(g);
                    b = srgb_scalar(b);
                }
                const float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                y_buf[i] = static_cast<uint8_t>(std::clamp(std::round(yy * 219.0f + 16.5f), 0.0f, 255.0f));
            }
            std::memcpy(dst_y + x + lanes, y_buf, static_cast<size_t>(lanes));

            hn::StoreU(r2, df, r_buf);
            hn::StoreU(g2, df, g_buf);
            hn::StoreU(b2, df, b_buf);
            for (int i = 0; i < lanes; ++i) {
                float r = std::clamp(r_buf[i], 0.0f, 1.0f);
                float g = std::clamp(g_buf[i], 0.0f, 1.0f);
                float b = std::clamp(b_buf[i], 0.0f, 1.0f);
                if (apply_gamma) {
                    r = srgb_scalar(r);
                    g = srgb_scalar(g);
                    b = srgb_scalar(b);
                }
                const float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                y_buf[i] = static_cast<uint8_t>(std::clamp(std::round(yy * 219.0f + 16.5f), 0.0f, 255.0f));
            }
            std::memcpy(dst_y + x + lanes * 2, y_buf, static_cast<size_t>(lanes));

            hn::StoreU(r3, df, r_buf);
            hn::StoreU(g3, df, g_buf);
            hn::StoreU(b3, df, b_buf);
            for (int i = 0; i < lanes; ++i) {
                float r = std::clamp(r_buf[i], 0.0f, 1.0f);
                float g = std::clamp(g_buf[i], 0.0f, 1.0f);
                float b = std::clamp(b_buf[i], 0.0f, 1.0f);
                if (apply_gamma) {
                    r = srgb_scalar(r);
                    g = srgb_scalar(g);
                    b = srgb_scalar(b);
                }
                const float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                y_buf[i] = static_cast<uint8_t>(std::clamp(std::round(yy * 219.0f + 16.5f), 0.0f, 255.0f));
            }
            std::memcpy(dst_y + x + lanes * 3, y_buf, static_cast<size_t>(lanes));
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
