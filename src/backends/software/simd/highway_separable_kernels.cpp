#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_separable_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/simd/kernels.hpp>
#include <cmath>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// ── Porter-Duff separable blend helper ──────────────────────────────────

// Helper: un-premultiply RGB.  Returns zero for alpha near zero.
HWY_ATTR hn::Vec<hn::FixedTag<float, 4>> safe_unpremul_rgb(
    hn::Vec<hn::FixedTag<float, 4>> premul,
    hn::Vec<hn::FixedTag<float, 4>> alpha_bb,
    const hn::FixedTag<float, 4>& d4,
    const hn::Vec<hn::FixedTag<float, 4>>& epsilon) {
    // Safe division: where alpha <= epsilon, result is 0.
    const auto safe_div = hn::Div(premul, alpha_bb);
    const auto mask = hn::Gt(alpha_bb, epsilon);
    return hn::IfThenElse(mask, safe_div, hn::Zero(d4));
}

HWY_ATTR void composite_darken_premul_impl(float* HWY_RESTRICT dst_ptr,
                                            const float* HWY_RESTRICT src_ptr,
                                            int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);  // premul src
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);   // premul dst

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);

        // Alpha: Ao = As + Ab - As*Ab = Ab + As*(1 - Ab)
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        // Un-premultiply to straight
        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Blend function: Darken = min(Cb, Cs)
        const auto B = hn::Min(Cb_rgb, Cs_rgb);

        // Porter-Duff over: Co = d * (1-As) + s * (1-Ab) + B * (As * Ab)
        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);             // d * (1-As)
        Co = hn::MulAdd(s, inv_Ab, Co);           // + s * (1-Ab)
        Co = hn::MulAdd(B, AsAb, Co);             // + B * As*Ab

        // Replace alpha.
        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_lighten_premul_impl(float* HWY_RESTRICT dst_ptr,
                                             const float* HWY_RESTRICT src_ptr,
                                             int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Lighten = max(Cb, Cs)
        const auto B = hn::Max(Cb_rgb, Cs_rgb);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_difference_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Difference = |Cb - Cs|
        const auto diff = hn::Sub(Cb_rgb, Cs_rgb);
        const auto B = hn::Abs(diff);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_exclusion_premul_impl(float* HWY_RESTRICT dst_ptr,
                                               const float* HWY_RESTRICT src_ptr,
                                               int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto two = hn::Set(d4, 2.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Exclusion = Cb + Cs - 2*Cb*Cs
        const auto prod = hn::Mul(Cb_rgb, Cs_rgb);
        const auto sum = hn::Add(Cb_rgb, Cs_rgb);
        const auto B = hn::Sub(sum, hn::Mul(prod, two));

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── SoftLight SIMD ─────────────────────────────────────────────────

HWY_ATTR void composite_soft_light_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto two  = hn::Set(d4, 2.0f);
    const auto half = hn::Set(d4, 0.5f);
    const auto quarter = hn::Set(d4, 0.25f);
    const auto c16 = hn::Set(d4, 16.0f);
    const auto c12 = hn::Set(d4, 12.0f);
    const auto c4  = hn::Set(d4, 4.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);           // premul src
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);           // premul dst

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        // Un-premultiply
        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1] per HDR contract
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // soft_light_d(cb): if cb <= 0.25 → ((16*cb - 12)*cb + 4)*cb  else sqrt(cb)
        auto d_val = hn::Sqrt(Cb_clamped);
        {
            auto poly = hn::Mul(Cb_clamped, hn::Sub(hn::Mul(Cb_clamped, c16), c12));
            poly = hn::Add(poly, c4);
            poly = hn::Mul(poly, Cb_clamped);
            const auto mask = hn::Le(Cb_clamped, quarter);
            d_val = hn::IfThenElse(mask, poly, d_val);
        }

        // soft_light(cb, cs): if cs <= 0.5 → cb - (1-2*cs)*cb*(1-cb)  else cb + (2*cs-1)*(d(cb)-cb)
        const auto om_cs = hn::Sub(one, Cs_clamped);
        const auto branch1 = hn::Sub(Cb_clamped,
                                      hn::Mul(hn::Mul(hn::Sub(one, hn::Mul(two, Cs_clamped)), Cb_clamped),
                                              hn::Sub(one, Cb_clamped)));
        const auto branch2 = hn::Add(Cb_clamped,
                                      hn::Mul(hn::Sub(hn::Mul(two, Cs_clamped), one),
                                              hn::Sub(d_val, Cb_clamped)));
        const auto B = hn::IfThenElse(hn::Le(Cs_clamped, half), branch1, branch2);

        // Porter-Duff over
        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── HardLight SIMD ────────────────────────────────────────────────

HWY_ATTR void composite_hard_light_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto two  = hn::Set(d4, 2.0f);
    const auto half = hn::Set(d4, 0.5f);
    const auto eps  = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1]
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // if cs <= 0.5 → 2*cb*cs  else 1 - 2*(1-cb)*(1-cs)
        const auto om_cb = hn::Sub(one, Cb_clamped);
        const auto om_cs = hn::Sub(one, Cs_clamped);
        const auto branch1 = hn::Mul(hn::Mul(two, Cb_clamped), Cs_clamped);
        const auto branch2 = hn::Sub(one, hn::Mul(hn::Mul(two, om_cb), om_cs));
        const auto B = hn::IfThenElse(hn::Le(Cs_clamped, half), branch1, branch2);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── ColorDodge SIMD ───────────────────────────────────────────────

HWY_ATTR void composite_color_dodge_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                 const float* HWY_RESTRICT src_ptr,
                                                 int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto eps  = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1]
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // dodge: if cs >= 1 → 1, if cb <= 0 → 0, else min(1, cb/(1-cs))
        const auto om_cs = hn::Sub(one, Cs_clamped);
        const auto dodge_val = hn::Div(Cb_clamped, hn::Max(om_cs, eps));
        const auto dodged = hn::Min(dodge_val, one);

        // cs >= 1 → 1
        const auto cs_ge_1 = hn::Ge(Cs_clamped, one);
        // cb <= 0 → 0
        const auto cb_le_0 = hn::Le(Cb_clamped, hn::Zero(d4));

        auto B = hn::IfThenElse(cb_le_0, hn::Zero(d4), dodged);
        B = hn::IfThenElse(cs_ge_1, one, B);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── ColorBurn SIMD ────────────────────────────────────────────────

HWY_ATTR void composite_color_burn_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto eps  = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1]
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // burn: if cs <= 0 → 0, if cb >= 1 → 1, else 1 - min(1, (1-cb)/cs)
        const auto om_cb = hn::Sub(one, Cb_clamped);
        const auto burn_val = hn::Div(om_cb, hn::Max(Cs_clamped, eps));
        const auto burned = hn::Sub(one, hn::Min(burn_val, one));

        const auto cs_le_0 = hn::Le(Cs_clamped, hn::Zero(d4));
        const auto cb_ge_1 = hn::Ge(Cb_clamped, one);

        // Match reference (blend_math.hpp): cs <= 0 takes priority over cb >= 1.
        auto B = hn::IfThenElse(cb_ge_1, one, burned);
        B = hn::IfThenElse(cs_le_0, hn::Zero(d4), B);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {
HWY_EXPORT(composite_darken_premul_impl);
HWY_EXPORT(composite_lighten_premul_impl);
HWY_EXPORT(composite_difference_premul_impl);
HWY_EXPORT(composite_exclusion_premul_impl);
HWY_EXPORT(composite_soft_light_premul_impl);
HWY_EXPORT(composite_hard_light_premul_impl);
HWY_EXPORT(composite_color_dodge_premul_impl);
HWY_EXPORT(composite_color_burn_premul_impl);

// Public wrapper functions: highway_color_kernels.cpp calls these directly.
// The wrappers perform the HWY_DYNAMIC_DISPATCH lookup here, in the translation
// unit where HWY_EXPORT made the dispatch tables visible.
void composite_darken_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                       const float* HWY_RESTRICT src_ptr,
                                       int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_darken_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_lighten_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                        const float* HWY_RESTRICT src_ptr,
                                        int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_lighten_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_difference_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                           const float* HWY_RESTRICT src_ptr,
                                           int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_difference_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_exclusion_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                          const float* HWY_RESTRICT src_ptr,
                                          int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_exclusion_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_soft_light_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                           const float* HWY_RESTRICT src_ptr,
                                           int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_soft_light_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_hard_light_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                           const float* HWY_RESTRICT src_ptr,
                                           int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_hard_light_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_color_dodge_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                            const float* HWY_RESTRICT src_ptr,
                                            int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_color_dodge_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_color_burn_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                           const float* HWY_RESTRICT src_ptr,
                                           int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_color_burn_premul_impl)(dst_ptr, src_ptr, pixel_count);
}
}  // namespace chronon3d::simd
#endif
