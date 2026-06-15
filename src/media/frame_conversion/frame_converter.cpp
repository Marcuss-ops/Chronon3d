#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/direct_yuv_converter.hpp>
#include "internal/yuv_conversion_internal.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/parallel_for.h>

#ifdef CHRONON3D_ENABLE_LIBYUV
// libyuv forward declarations — C linkage, no header conflicts.
extern "C" {
int ABGRToI420(const uint8_t* src_abgr, int src_stride_abgr,
               uint8_t* dst_y, int dst_stride_y,
               uint8_t* dst_u, int dst_stride_u,
               uint8_t* dst_v, int dst_stride_v,
               int width, int height);
int ABGRToNV12(const uint8_t* src_abgr, int src_stride_abgr,
               uint8_t* dst_y, int dst_stride_y,
               uint8_t* dst_uv, int dst_stride_uv,
               int width, int height);
}
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace chronon3d::video {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
struct SwsParams {
    int          src_w, src_h;
    AVPixelFormat src_fmt;
    int          dst_w, dst_h;
    AVPixelFormat dst_fmt;
    int          color_matrix;   // 0 = BT.709, 1 = BT.601, 2 = BT.2020
};

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const noexcept {
        if (ctx) sws_freeContext(ctx);
    }
};

static SwsContext* get_or_create_sws_context(const SwsParams& params) {
    thread_local std::unique_ptr<SwsContext, SwsContextDeleter> ctx_owner;

    SwsContext* ctx = ctx_owner.release();
    ctx = sws_getCachedContext(
        ctx,
        params.src_w, params.src_h, params.src_fmt,
        params.dst_w, params.dst_h, params.dst_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!ctx)
        return nullptr;
    ctx_owner.reset(ctx);  // takes ownership (frees old ctx if reallocated)


    const int src_cs = (params.color_matrix == 1) ? SWS_CS_DEFAULT :
                       (params.color_matrix == 2) ? SWS_CS_BT2020 : SWS_CS_ITU709;
    const int dst_cs = (params.color_matrix == 1) ? SWS_CS_DEFAULT :
                       (params.color_matrix == 2) ? SWS_CS_BT2020 : SWS_CS_ITU709;

    const int* src_coeff = sws_getCoefficients(src_cs);
    const int* dst_coeff = sws_getCoefficients(dst_cs);
    const int dst_range = (params.dst_fmt == AV_PIX_FMT_RGB24) ? 1 : 0;
    sws_setColorspaceDetails(ctx, src_coeff, 1, dst_coeff, dst_range, 0, 1 << 16, 1 << 16);
    return ctx;
}
#endif

static std::vector<uint8_t>& rgba8_staging(size_t min_bytes) {
    thread_local std::vector<uint8_t> buf;
    if (buf.size() < min_bytes) buf.resize(min_bytes);
    return buf;
}

static inline uint64_t now_ns() {
    return profiling::timestamp_ns();
}

void convert_fb_to_rgba8_public(const Framebuffer& src, int width, int height, bool apply_gamma, uint8_t* rgba8) {
    const Color* src_data = src.data();
    const int alloc_w = src.allocated_width();
    const int grain = std::max(32, height / 16);
    parallel_for_tracked(tbb::blocked_range<int>(0, height, grain), [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            const Color* src_row = src_data + static_cast<size_t>(y) * alloc_w;
            uint8_t* dst_row = rgba8 + static_cast<size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                const Color& c = src_row[x];
                if (apply_gamma) {
                    dst_row[x * 4 + 0] = Color::linear_to_srgb8(c.r);
                    dst_row[x * 4 + 1] = Color::linear_to_srgb8(c.g);
                    dst_row[x * 4 + 2] = Color::linear_to_srgb8(c.b);
                } else {
                    dst_row[x * 4 + 0] = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                    dst_row[x * 4 + 1] = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                    dst_row[x * 4 + 2] = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
                dst_row[x * 4 + 3] = static_cast<uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    });
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
static ConvertFrameResult convert_rgba_to_target(const ConvertFrameRequest& req, AVPixelFormat dst_avfmt) {
    const uint64_t t0 = now_ns();
    const size_t rgba_bytes = static_cast<size_t>(req.width) * req.height * 4;
    auto& staging = rgba8_staging(rgba_bytes);
    convert_fb_to_rgba8_public(req.src, req.width, req.height, req.apply_gamma, staging.data());

    const SwsParams params{req.width, req.height, AV_PIX_FMT_RGBA, req.width, req.height, dst_avfmt, req.color_matrix};
    SwsContext* ctx = get_or_create_sws_context(params);
    if (!ctx) return ConvertFrameResult{.success = false};

    uint8_t* src_data[4] = { staging.data(), nullptr, nullptr, nullptr };
    int src_stride[4] = { req.width * 4, 0, 0, 0 };
    uint8_t* dst_data[4] = { nullptr, nullptr, nullptr, nullptr };
    int dst_stride[4] = { 0, 0, 0, 0 };

    switch (req.format) {
        case EncoderPixelFormat::YUV420P:
            dst_data[0] = req.dst_y; dst_data[1] = req.dst_u; dst_data[2] = req.dst_v;
            dst_stride[0] = req.dst_stride_y ? req.dst_stride_y : req.width;
            dst_stride[1] = req.dst_stride_u ? req.dst_stride_u : (req.width / 2);
            dst_stride[2] = req.dst_stride_v ? req.dst_stride_v : (req.width / 2);
            break;
        case EncoderPixelFormat::NV12:
            dst_data[0] = req.dst_y; dst_data[1] = req.dst_uv;
            dst_stride[0] = req.dst_stride_y ? req.dst_stride_y : req.width;
            dst_stride[1] = req.dst_stride_uv ? req.dst_stride_uv : req.width;
            break;
        case EncoderPixelFormat::RGB24:
            dst_data[0] = req.dst_y;
            dst_stride[0] = req.dst_stride_y ? req.dst_stride_y : req.width * 3;
            break;
    }
    const int ret = sws_scale(ctx, src_data, src_stride, 0, req.height, dst_data, dst_stride);
    return ConvertFrameResult{ .success = (ret == req.height), .used_simd = true, .conversion_ns = now_ns() - t0 };
}
#endif

ConvertFrameResult convert_rgba_to_yuv420p_swscale(const ConvertFrameRequest& req) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if (req.width % 2 != 0 || req.height % 2 != 0) return ConvertFrameResult{.success = false};
    return convert_rgba_to_target(req, AV_PIX_FMT_YUV420P);
#else
    (void)req; return ConvertFrameResult{.success = false};
#endif
}

ConvertFrameResult convert_rgba_to_nv12_swscale(const ConvertFrameRequest& req) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if (req.width % 2 != 0 || req.height % 2 != 0) return ConvertFrameResult{.success = false};
    return convert_rgba_to_target(req, AV_PIX_FMT_NV12);
#else
    (void)req; return ConvertFrameResult{.success = false};
#endif
}

static ConvertFrameResult convert_rgba_to_rgb24(const ConvertFrameRequest& req) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    return convert_rgba_to_target(req, AV_PIX_FMT_RGB24);
#else
    (void)req; return ConvertFrameResult{.success = false};
#endif
}

// ── libyuv RGBA8 → YUV420P ─────────────────────────────────────────────────
#ifdef CHRONON3D_ENABLE_LIBYUV
static ConvertFrameResult convert_rgba_to_yuv420p_libyuv(const ConvertFrameRequest& req) {
    if (req.width % 2 != 0 || req.height % 2 != 0)
        return ConvertFrameResult{.success = false};
    if (!req.dst_y || !req.dst_u || !req.dst_v)
        return ConvertFrameResult{.success = false};

    const uint64_t t0 = now_ns();
    const size_t rgba_bytes = static_cast<size_t>(req.width) * req.height * 4;
    auto& staging = rgba8_staging(rgba_bytes);
    convert_fb_to_rgba8_public(req.src, req.width, req.height, req.apply_gamma, staging.data());

    const int src_stride = req.width * 4;
    const int dst_stride_y = req.dst_stride_y ? req.dst_stride_y : req.width;
    const int dst_stride_u = req.dst_stride_u ? req.dst_stride_u : (req.width / 2);
    const int dst_stride_v = req.dst_stride_v ? req.dst_stride_v : (req.width / 2);

    const int ret = ABGRToI420(
        staging.data(), src_stride,
        req.dst_y, dst_stride_y,
        req.dst_u, dst_stride_u,
        req.dst_v, dst_stride_v,
        req.width, req.height);

    return ConvertFrameResult{
        .success = (ret == 0),
        .used_simd = true,
        .conversion_ns = now_ns() - t0,
    };
}

#else
static ConvertFrameResult convert_rgba_to_yuv420p_libyuv(const ConvertFrameRequest&) {
    return ConvertFrameResult{.success = false};
}
#endif

// ── libyuv RGBA8 → NV12 ──────────────────────────────────────────────────
#ifdef CHRONON3D_ENABLE_LIBYUV
static ConvertFrameResult convert_rgba_to_nv12_libyuv(const ConvertFrameRequest& req) {
    if (req.width % 2 != 0 || req.height % 2 != 0)
        return ConvertFrameResult{.success = false};
    if (!req.dst_y || !req.dst_uv)
        return ConvertFrameResult{.success = false};

    const uint64_t t0 = now_ns();
    const size_t rgba_bytes = static_cast<size_t>(req.width) * req.height * 4;
    auto& staging = rgba8_staging(rgba_bytes);
    convert_fb_to_rgba8_public(req.src, req.width, req.height, req.apply_gamma, staging.data());

    const int src_stride = req.width * 4;
    const int dst_stride_y  = req.dst_stride_y  ? req.dst_stride_y  : req.width;
    const int dst_stride_uv = req.dst_stride_uv ? req.dst_stride_uv : req.width;

    const int ret = ABGRToNV12(
        staging.data(), src_stride,
        req.dst_y, dst_stride_y,
        req.dst_uv, dst_stride_uv,
        req.width, req.height);

    return ConvertFrameResult{
        .success = (ret == 0),
        .used_simd = true,
        .conversion_ns = now_ns() - t0,
    };
}
#else
static ConvertFrameResult convert_rgba_to_nv12_libyuv(const ConvertFrameRequest&) {
    return ConvertFrameResult{.success = false};
}
#endif

ConvertFrameResult convert_frame(const ConvertFrameRequest& req) {
    // ── YUV420P / NV12 ──────────────────────────────────────────────────
    // Primary path: direct float→YUV via Highway SIMD (single-pass, no
    // intermediate RGBA8 buffer).  Fallback to libyuv for platforms where
    // the direct path is unavailable or broken.  Final fallback to sws_scale.
    if (req.format == EncoderPixelFormat::YUV420P || req.format == EncoderPixelFormat::NV12) {
        DirectYuvRequest dreq{
            .src = req.src, .dst_y = req.dst_y, .dst_u = req.dst_u, .dst_v = req.dst_v, .dst_uv = req.dst_uv,
            .dst_stride_y = req.dst_stride_y, .dst_stride_u = req.dst_stride_u, .dst_stride_v = req.dst_stride_v, .dst_stride_uv = req.dst_stride_uv,
            .width = req.width, .height = req.height, .format = req.format, .apply_gamma = req.apply_gamma, .color_matrix = req.color_matrix,
        };
        auto direct = convert_framebuffer_to_yuv_direct(dreq);
        if (direct.success) {
            return ConvertFrameResult{ .success = true, .used_simd = direct.used_simd, .conversion_ns = direct.conversion_ns };
        }

        // libyuv only supports the default color matrix (BT.709 for HD, BT.601
        // for SD). If a custom color matrix is requested, skip libyuv.
        if (req.color_matrix == 0) {
            if (req.format == EncoderPixelFormat::YUV420P) {
                auto libyuv = convert_rgba_to_yuv420p_libyuv(req);
                if (libyuv.success) return libyuv;
            } else {
#ifdef CHRONON3D_ENABLE_LIBYUV
                auto libyuv = convert_rgba_to_nv12_libyuv(req);
                if (libyuv.success) return libyuv;
#else
                (void)convert_rgba_to_nv12_libyuv(req);
#endif
            }
        }
    }

    // ── RGBA8: direct float→uint8 with gamma (no conversion needed) ─────
    if (req.format == EncoderPixelFormat::RGBA8) {
        const uint64_t t0 = now_ns();
        convert_fb_to_rgba8_public(req.src, req.width, req.height, req.apply_gamma, req.dst_y);
        return ConvertFrameResult{
            .success = true,
            .used_simd = true,
            .conversion_ns = now_ns() - t0,
        };
    }

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    switch (req.format) {
        case EncoderPixelFormat::YUV420P: return convert_rgba_to_target(req, AV_PIX_FMT_YUV420P);
        case EncoderPixelFormat::NV12:    return convert_rgba_to_target(req, AV_PIX_FMT_NV12);
        case EncoderPixelFormat::RGB24:   return convert_rgba_to_target(req, AV_PIX_FMT_RGB24);
        case EncoderPixelFormat::RGBA8:   // Handled above — never reached
            break;
    }
#else
    if (req.format == EncoderPixelFormat::RGB24) {
        // Fallback for RGB24 without FFmpeg? For now just fail.
        return ConvertFrameResult{.success = false};
    }
#endif
    return ConvertFrameResult{.success = false};
}

ConvertFrameResult convert_frame_tight(const Framebuffer& src, uint8_t* dst_y, uint8_t* dst_u, uint8_t* dst_v, uint8_t* dst_uv, int width, int height, EncoderPixelFormat format, bool apply_gamma) {
    // Compute tight stride: RGBA8 = width*4, RGB24 = width*3, YUV = width.
    const int tight_stride_y = (format == EncoderPixelFormat::RGBA8) ? (width * 4)
                            : (format == EncoderPixelFormat::RGB24) ? (width * 3)
                            : width;
    ConvertFrameRequest req{
        .src = src, .dst_y = dst_y, .dst_u = dst_u, .dst_v = dst_v, .dst_uv = dst_uv,
        .dst_stride_y = tight_stride_y,
        .dst_stride_uv = width, .dst_stride_u = width / 2, .dst_stride_v = width / 2,
        .color_matrix = 0, .width = width, .height = height, .format = format, .apply_gamma = apply_gamma,
    };
    return convert_frame(req);
}

} // namespace chronon3d::video
