#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_color_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

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

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {

HWY_EXPORT(composite_normal_premul_impl);
HWY_EXPORT(premultiply_alpha_rgba8_impl);
HWY_EXPORT(convert_f32_rgba_to_u8_rgba_impl);

void composite_normal_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(premultiply_alpha_rgba8_impl)(dst, src, pixel_count);
}

void convert_f32_rgba_to_u8_rgba(uint8_t* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(convert_f32_rgba_to_u8_rgba_impl)(dst, reinterpret_cast<const float*>(src), pixel_count);
}

void clear_framebuffer(Color* data, int pixel_count, const Color& color) {
    std::fill(data, data + pixel_count, color);
}

}  // namespace chronon3d::simd
#endif
