#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_rasterize_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <cmath>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

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

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {

HWY_EXPORT(rasterize_rect_simd_impl);

void rasterize_rect_simd(Color* __restrict__ row, const float* __restrict__ lp_h_start, const float* __restrict__ col0, int pixel_count, float rect_w, float rect_h, float spread, const Color& color) {
    HWY_DYNAMIC_DISPATCH(rasterize_rect_simd_impl)(reinterpret_cast<float*>(row), lp_h_start, col0, pixel_count, rect_w, rect_h, spread, reinterpret_cast<const float*>(&color));
}

}  // namespace chronon3d::simd
#endif
