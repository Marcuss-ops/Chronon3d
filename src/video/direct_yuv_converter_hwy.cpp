// ============================================================================
// direct_yuv_converter_hwy.cpp — Highway SIMD-accelerated YUV converter.
//
// This is the active SIMD backend for direct float Framebuffer → YUV420P/NV12.
// It is called by convert_framebuffer_to_yuv_direct() before the scalar TBB
// fallback.  Each tbb parallel task processes 2-row blocks.  Within each
// block the inner loop uses Highway SIMD for the BT.709/601 matrix (FMA).
// The gamma LUT + byte→int32 widening is scalar but amortised over SIMD FMA.
//
// Call chain:
//   CLI video export → FfmpegPipeEncoder → convert_frame_tight
//     → convert_frame → convert_framebuffer_to_yuv_direct
//       → convert_to_yuv420p_hwy / convert_to_nv12_hwy  (this file)
//       → scalar TBB fallback (direct_yuv_converter.cpp)
// ============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/video/direct_yuv_converter_hwy.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/video/direct_yuv_converter.hpp>
#include <chronon3d/video/direct_yuv_lut.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>

#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::video {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// ── Type tags ─────────────────────────────────────────────────────────────
using DF   = hn::ScalableTag<float>;
using DI32 = hn::ScalableTag<int32_t>;

// ============================================================================
//  Load N uint8 values into a float vector (scalar widening).
//
//  HWY doesn't support U8→I32 promotion on pre-SSE4.1 targets, so we
//  widen manually.  The overhead is O(N) scalar loads, which is negligible
//  compared to the subsequent SIMD FMA.
// ============================================================================

template <int N>
static hn::Vec<DF> load_u8_to_float(const uint8_t* HWY_RESTRICT arr) {
    HWY_ALIGN int32_t buf[16];
    for (int i = 0; i < N; ++i) buf[i] = arr[i];
    return hn::ConvertTo(DF(), hn::LoadU(DI32(), buf));
}

// ============================================================================
//  Process a 2-row × N-column block.
// ============================================================================

static void process_block_hwy(const DirectYuvRequest& req,
                               int y, int w,
                               const YuvCoeffs& coeffs) {
    const DF   df;
    const int  N = hn::Lanes(df);  // 4 (SSE4) or 8 (AVX2) or 16 (AVX-512)

    const Framebuffer& src         = req.src;
    const int          src_stride  = src.allocated_width();
    const Color*       src_data    = src.data();
    const bool         apply_gamma = req.apply_gamma;

    const int stride_y = req.dst_stride_y ? req.dst_stride_y : w;
    const int stride_u = req.dst_stride_u ? req.dst_stride_u : (w / 2);
    const int stride_v = req.dst_stride_v ? req.dst_stride_v : (w / 2);

    // ── Matrix coefficients ────────────────────────────────────────────
    // All three Cb/Cr coefficients sum to 0 (neutral grey → 128/128).
    // Coefficients are from YuvCoeffs in direct_yuv_lut.hpp.
    const float y_kr = coeffs.kr * 219.0f / 255.0f;
    const float y_kg = coeffs.kg * 219.0f / 255.0f;
    const float y_kb = coeffs.kb * 219.0f / 255.0f;
    const float u_cr = coeffs.cb_r * 224.0f / 255.0f;
    const float u_cg = coeffs.cb_g * 224.0f / 255.0f;
    const float u_cb = coeffs.cb_b * 224.0f / 255.0f;
    const float v_cr = coeffs.cr_r * 224.0f / 255.0f;
    const float v_cg = coeffs.cr_g * 224.0f / 255.0f;
    const float v_cb = coeffs.cr_b * 224.0f / 255.0f;

    const auto v_y_kr  = hn::Set(df, y_kr);
    const auto v_y_kg  = hn::Set(df, y_kg);
    const auto v_y_kb  = hn::Set(df, y_kb);
    const auto v_u_cr  = hn::Set(df, u_cr);
    const auto v_u_cg  = hn::Set(df, u_cg);
    const auto v_u_cb  = hn::Set(df, u_cb);
    const auto v_v_cr  = hn::Set(df, v_cr);
    const auto v_v_cg  = hn::Set(df, v_cg);
    const auto v_v_cb  = hn::Set(df, v_cb);
    const auto v_y_off = hn::Set(df, 16.5f);
    const auto v_uv_off= hn::Set(df, 128.5f);
    const auto v_255   = hn::Set(df, 255.0f);
    const auto v_zero  = hn::Zero(df);

    const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
    const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

    uint8_t* dst_y0 = req.dst_y + static_cast<size_t>(y) * stride_y;
    uint8_t* dst_y1 = req.dst_y + static_cast<size_t>(y + 1) * stride_y;
    uint8_t* dst_u  = req.dst_u + static_cast<size_t>(y / 2) * stride_u;
    uint8_t* dst_v  = req.dst_v + static_cast<size_t>(y / 2) * stride_v;

    HWY_ALIGN uint8_t r0[16], g0[16], b0[16];
    HWY_ALIGN uint8_t r1[16], g1[16], b1[16];
    HWY_ALIGN uint8_t u0[16], v0[16], u1[16], v1[16];
    HWY_ALIGN int32_t ys[16];

    int x = 0;
    while (x < w) {
        const int chunk = std::min(N, w - x);

        // ── Phase 1: Gamma LUT (scalar) ────────────────────────────────
        for (int i = 0; i < chunk; ++i) {
            const Color& c0 = src_row0[x + i];
            const Color& c1 = src_row1[x + i];
            if (apply_gamma) {
                r0[i] = linear_to_srgb8_fast(static_cast<float>(c0.r));
                g0[i] = linear_to_srgb8_fast(static_cast<float>(c0.g));
                b0[i] = linear_to_srgb8_fast(static_cast<float>(c0.b));
                r1[i] = linear_to_srgb8_fast(static_cast<float>(c1.r));
                g1[i] = linear_to_srgb8_fast(static_cast<float>(c1.g));
                b1[i] = linear_to_srgb8_fast(static_cast<float>(c1.b));
            } else {
                auto to8 = [](float v){return static_cast<uint8_t>(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);};
                    r0[i] = to8(static_cast<float>(c0.r));
                    g0[i] = to8(static_cast<float>(c0.g));
                    b0[i] = to8(static_cast<float>(c0.b));
                    r1[i] = to8(static_cast<float>(c1.r));
                    g1[i] = to8(static_cast<float>(c1.g));
                    b1[i] = to8(static_cast<float>(c1.b));
            }
        }
        for (int i = chunk; i < N; ++i)
            r0[i]=r0[chunk-1],g0[i]=g0[chunk-1],b0[i]=b0[chunk-1];

        // ── Phase 2: Row 0 (Y, U, V) via HWY SIMD ─────────────────────
        {
            const auto r_f = load_u8_to_float<16>(r0);
            const auto g_f = load_u8_to_float<16>(g0);
            const auto b_f = load_u8_to_float<16>(b0);

            auto y_f = hn::Min(hn::Max(
                hn::MulAdd(b_f, v_y_kb,
                hn::MulAdd(g_f, v_y_kg,
                hn::MulAdd(r_f, v_y_kr, v_y_off))), v_zero), v_255);
            hn::Store(hn::ConvertTo(DI32(), y_f), DI32(), ys);
            for (int i = 0; i < chunk; ++i) dst_y0[x+i] = static_cast<uint8_t>(ys[i]);

            auto u_f = hn::Min(hn::Max(
                hn::MulAdd(b_f, v_u_cb,
                hn::MulAdd(g_f, v_u_cg,
                hn::MulAdd(r_f, v_u_cr, v_uv_off))), v_zero), v_255);
            hn::Store(hn::ConvertTo(DI32(), u_f), DI32(), ys);
            for (int i = 0; i < chunk; ++i) u0[i] = static_cast<uint8_t>(ys[i]);

            auto v_f = hn::Min(hn::Max(
                hn::MulAdd(b_f, v_v_cb,
                hn::MulAdd(g_f, v_v_cg,
                hn::MulAdd(r_f, v_v_cr, v_uv_off))), v_zero), v_255);
            hn::Store(hn::ConvertTo(DI32(), v_f), DI32(), ys);
            for (int i = 0; i < chunk; ++i) v0[i] = static_cast<uint8_t>(ys[i]);
        }

        // ── Phase 3: Row 1 (Y, U, V) via HWY SIMD ─────────────────────
        {
            const auto r_f = load_u8_to_float<16>(r1);
            const auto g_f = load_u8_to_float<16>(g1);
            const auto b_f = load_u8_to_float<16>(b1);

            auto y_f = hn::Min(hn::Max(
                hn::MulAdd(b_f, v_y_kb,
                hn::MulAdd(g_f, v_y_kg,
                hn::MulAdd(r_f, v_y_kr, v_y_off))), v_zero), v_255);
            hn::Store(hn::ConvertTo(DI32(), y_f), DI32(), ys);
            for (int i = 0; i < chunk; ++i) dst_y1[x+i] = static_cast<uint8_t>(ys[i]);

            auto u_f = hn::Min(hn::Max(
                hn::MulAdd(b_f, v_u_cb,
                hn::MulAdd(g_f, v_u_cg,
                hn::MulAdd(r_f, v_u_cr, v_uv_off))), v_zero), v_255);
            hn::Store(hn::ConvertTo(DI32(), u_f), DI32(), ys);
            for (int i = 0; i < chunk; ++i) u1[i] = static_cast<uint8_t>(ys[i]);

            auto v_f = hn::Min(hn::Max(
                hn::MulAdd(b_f, v_v_cb,
                hn::MulAdd(g_f, v_v_cg,
                hn::MulAdd(r_f, v_v_cr, v_uv_off))), v_zero), v_255);
            hn::Store(hn::ConvertTo(DI32(), v_f), DI32(), ys);
            for (int i = 0; i < chunk; ++i) v1[i] = static_cast<uint8_t>(ys[i]);
        }

        // ── Phase 4: Chroma 4:2:0 averaging ────────────────────────────
        for (int i = 0; i < chunk; i += 2) {
            if (x + i + 1 >= w) break;
            const unsigned s_u = static_cast<unsigned>(u0[i])+u0[i+1]+u1[i]+u1[i+1];
            const unsigned s_v = static_cast<unsigned>(v0[i])+v0[i+1]+v1[i]+v1[i+1];
            dst_u[x/2 + i/2] = static_cast<uint8_t>((s_u+2)/4);
            dst_v[x/2 + i/2] = static_cast<uint8_t>((s_v+2)/4);
        }
        x += chunk;
    }
}

// ============================================================================
//  YUV420P entry point
// ============================================================================

HWY_ATTR DirectYuvResult convert_to_yuv420p_hwy_impl(const DirectYuvRequest& req) {
    const uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();
    if (req.width % 2 != 0 || req.height % 2 != 0) return DirectYuvResult{};
    if (!req.dst_y || !req.dst_u || !req.dst_v)    return DirectYuvResult{};

    const auto& coeffs = get_coeffs(req.color_matrix);
    const int nb = req.height / 2;

    parallel_for_tracked(tbb::blocked_range<int>(0, nb), [&](auto& r) {
        for (int b = r.begin(); b < r.end(); ++b)
            process_block_hwy(req, b*2, req.width, coeffs);
    });

    const uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    return DirectYuvResult{.success = true, .used_simd = true, .conversion_ns = t1 - t0};
}

// ============================================================================
//  NV12 entry point
// ============================================================================

HWY_ATTR DirectYuvResult convert_to_nv12_hwy_impl(const DirectYuvRequest& req) {
    const uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();
    if (req.width % 2 != 0 || req.height % 2 != 0) return DirectYuvResult{};
    if (!req.dst_y || !req.dst_uv)                  return DirectYuvResult{};

    const auto& coeffs = get_coeffs(req.color_matrix);
    const int src_stride = req.src.allocated_width();
    const Color* src_data = req.src.data();
    const bool apply_gamma = req.apply_gamma;
    const int stride_y  = req.dst_stride_y  ? req.dst_stride_y  : req.width;
    const int stride_uv = req.dst_stride_uv ? req.dst_stride_uv : req.width;
    const int nb = req.height / 2;

    parallel_for_tracked(tbb::blocked_range<int>(0, nb), [&](auto& r) {
        const DF   df;
        const int  N = hn::Lanes(df);

        const float y_kr = coeffs.kr * 219.0f / 255.0f;
        const float y_kg = coeffs.kg * 219.0f / 255.0f;
        const float y_kb = coeffs.kb * 219.0f / 255.0f;
        const float u_cr = coeffs.cb_r * 224.0f / 255.0f;
        const float u_cg = coeffs.cb_g * 224.0f / 255.0f;
        const float u_cb = coeffs.cb_b * 224.0f / 255.0f;
        const float v_cr = coeffs.cr_r * 224.0f / 255.0f;
        const float v_cg = coeffs.cr_g * 224.0f / 255.0f;
        const float v_cb = coeffs.cr_b * 224.0f / 255.0f;

        const auto v_y_kr  = hn::Set(df, y_kr);
        const auto v_y_kg  = hn::Set(df, y_kg);
        const auto v_y_kb  = hn::Set(df, y_kb);
        const auto v_u_cr  = hn::Set(df, u_cr);
        const auto v_u_cg  = hn::Set(df, u_cg);
        const auto v_u_cb  = hn::Set(df, u_cb);
        const auto v_v_cr  = hn::Set(df, v_cr);
        const auto v_v_cg  = hn::Set(df, v_cg);
        const auto v_v_cb  = hn::Set(df, v_cb);
        const auto v_y_off = hn::Set(df, 16.5f);
        const auto v_uv_off= hn::Set(df, 128.5f);
        const auto v_255   = hn::Set(df, 255.0f);
        const auto v_zero  = hn::Zero(df);

        HWY_ALIGN uint8_t r0[16], g0[16], b0[16], r1[16], g1[16], b1[16];
        HWY_ALIGN uint8_t u0[16], v0[16], u1[16], v1[16];
        HWY_ALIGN int32_t ys[16];

        for (int b = r.begin(); b < r.end(); ++b) {
            const int y = b * 2;
            const Color* s0 = src_data + static_cast<size_t>(y) * src_stride;
            const Color* s1 = src_data + static_cast<size_t>(y+1) * src_stride;
            uint8_t* dy0 = req.dst_y + static_cast<size_t>(y) * stride_y;
            uint8_t* dy1 = req.dst_y + static_cast<size_t>(y+1) * stride_y;
            uint8_t* duv = req.dst_uv + static_cast<size_t>(y/2) * stride_uv;

            int x = 0;
            while (x < req.width) {
                const int chunk = std::min(N, req.width - x);

                for (int i = 0; i < chunk; ++i) {
                    if (apply_gamma) {
                        r0[i]=linear_to_srgb8_fast(static_cast<float>(s0[x+i].r));
                        g0[i]=linear_to_srgb8_fast(static_cast<float>(s0[x+i].g));
                        b0[i]=linear_to_srgb8_fast(static_cast<float>(s0[x+i].b));
                        r1[i]=linear_to_srgb8_fast(static_cast<float>(s1[x+i].r));
                        g1[i]=linear_to_srgb8_fast(static_cast<float>(s1[x+i].g));
                        b1[i]=linear_to_srgb8_fast(static_cast<float>(s1[x+i].b));
                    } else {
                        auto t=[](float v){return static_cast<uint8_t>(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);};
                        r0[i]=t(static_cast<float>(s0[x+i].r));
                        g0[i]=t(static_cast<float>(s0[x+i].g));
                        b0[i]=t(static_cast<float>(s0[x+i].b));
                        r1[i]=t(static_cast<float>(s1[x+i].r));
                        g1[i]=t(static_cast<float>(s1[x+i].g));
                        b1[i]=t(static_cast<float>(s1[x+i].b));
                    }
                }
                for (int i = chunk; i < N; ++i)
                    r0[i]=r0[chunk-1],g0[i]=g0[chunk-1],b0[i]=b0[chunk-1];

                // Row 0
                {
                    auto rf=load_u8_to_float<16>(r0), gf=load_u8_to_float<16>(g0), bf=load_u8_to_float<16>(b0);
                    auto yf=hn::Min(hn::Max(hn::MulAdd(bf,v_y_kb,hn::MulAdd(gf,v_y_kg,hn::MulAdd(rf,v_y_kr,v_y_off))),v_zero),v_255);
                    hn::Store(hn::ConvertTo(DI32(),yf),DI32(),ys);
                    for(int i=0;i<chunk;++i) dy0[x+i]=static_cast<uint8_t>(ys[i]);
                    auto uf=hn::Min(hn::Max(hn::MulAdd(bf,v_u_cb,hn::MulAdd(gf,v_u_cg,hn::MulAdd(rf,v_u_cr,v_uv_off))),v_zero),v_255);
                    hn::Store(hn::ConvertTo(DI32(),uf),DI32(),ys);
                    for(int i=0;i<chunk;++i) u0[i]=static_cast<uint8_t>(ys[i]);
                    auto vf=hn::Min(hn::Max(hn::MulAdd(bf,v_v_cb,hn::MulAdd(gf,v_v_cg,hn::MulAdd(rf,v_v_cr,v_uv_off))),v_zero),v_255);
                    hn::Store(hn::ConvertTo(DI32(),vf),DI32(),ys);
                    for(int i=0;i<chunk;++i) v0[i]=static_cast<uint8_t>(ys[i]);
                }
                // Row 1
                {
                    auto rf=load_u8_to_float<16>(r1), gf=load_u8_to_float<16>(g1), bf=load_u8_to_float<16>(b1);
                    auto yf=hn::Min(hn::Max(hn::MulAdd(bf,v_y_kb,hn::MulAdd(gf,v_y_kg,hn::MulAdd(rf,v_y_kr,v_y_off))),v_zero),v_255);
                    hn::Store(hn::ConvertTo(DI32(),yf),DI32(),ys);
                    for(int i=0;i<chunk;++i) dy1[x+i]=static_cast<uint8_t>(ys[i]);
                    auto uf=hn::Min(hn::Max(hn::MulAdd(bf,v_u_cb,hn::MulAdd(gf,v_u_cg,hn::MulAdd(rf,v_u_cr,v_uv_off))),v_zero),v_255);
                    hn::Store(hn::ConvertTo(DI32(),uf),DI32(),ys);
                    for(int i=0;i<chunk;++i) u1[i]=static_cast<uint8_t>(ys[i]);
                    auto vf=hn::Min(hn::Max(hn::MulAdd(bf,v_v_cb,hn::MulAdd(gf,v_v_cg,hn::MulAdd(rf,v_v_cr,v_uv_off))),v_zero),v_255);
                    hn::Store(hn::ConvertTo(DI32(),vf),DI32(),ys);
                    for(int i=0;i<chunk;++i) v1[i]=static_cast<uint8_t>(ys[i]);
                }

                for (int i = 0; i < chunk; i += 2) {
                    if (x + i + 1 >= req.width) break;
                    const unsigned su = static_cast<unsigned>(u0[i])+u0[i+1]+u1[i]+u1[i+1];
                    const unsigned sv = static_cast<unsigned>(v0[i])+v0[i+1]+v1[i]+v1[i+1];
                    duv[x+i]   = static_cast<uint8_t>((su+2)/4);
                    duv[x+i+1] = static_cast<uint8_t>((sv+2)/4);
                }
                x += chunk;
            }
        }
    });

    const uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    return DirectYuvResult{.success = true, .used_simd = true, .conversion_ns = t1 - t0};
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::video
HWY_AFTER_NAMESPACE();

// ============================================================================
//  HWY_DYNAMIC_DISPATCH — best SIMD target at runtime
// ============================================================================

#if HWY_ONCE
namespace chronon3d::video {

HWY_EXPORT(convert_to_yuv420p_hwy_impl);
HWY_EXPORT(convert_to_nv12_hwy_impl);

DirectYuvResult convert_to_yuv420p_hwy(const DirectYuvRequest& req) {
    return HWY_DYNAMIC_DISPATCH(convert_to_yuv420p_hwy_impl)(req);
}

DirectYuvResult convert_to_nv12_hwy(const DirectYuvRequest& req) {
    return HWY_DYNAMIC_DISPATCH(convert_to_nv12_hwy_impl)(req);
}

} // namespace chronon3d::video
#endif
