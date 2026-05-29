#include <chronon3d/video/frame_converter.hpp>
#include <chronon3d/video/direct_yuv_converter.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <tbb/parallel_for.h>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}

namespace chronon3d::video {

// ============================================================================
//  SwsContext via sws_getCachedContext — adapts instantly when dimensions
//  change without destroying and recreating the context from scratch.
//
//  sws_getCachedContext checks internally whether the existing context matches
//  the requested parameters.  When they differ (even by 1 pixel), it re-adapts
//  the internal filter coefficients in-place instead of freeing + allocating.
// ============================================================================

struct SwsParams {
    int          src_w, src_h;
    AVPixelFormat src_fmt;
    int          dst_w, dst_h;
    AVPixelFormat dst_fmt;
    int          color_matrix;   // 0 = BT.709, 1 = BT.601, 2 = BT.2020
};

static std::mutex s_sws_mutex;

/// Retrieve (or create-and-cache) a SwsContext via sws_getCachedContext.
/// The context is configured with BT.601/BT.709/BT.2020 coefficients and
/// full-range (0-255) input/output.
static SwsContext* get_or_create_sws_context(const SwsParams& params) {
    std::lock_guard<std::mutex> lock(s_sws_mutex);

    static SwsContext* ctx = nullptr;

    ctx = sws_getCachedContext(
        ctx,
        params.src_w, params.src_h, params.src_fmt,
        params.dst_w, params.dst_h, params.dst_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!ctx)
        return nullptr;

    // Select color space coefficients.
    const int src_cs = (params.color_matrix == 1) ? SWS_CS_DEFAULT :
                       (params.color_matrix == 2) ? SWS_CS_BT2020 :
                       SWS_CS_ITU709;
    const int dst_cs = (params.color_matrix == 1) ? SWS_CS_DEFAULT :
                       (params.color_matrix == 2) ? SWS_CS_BT2020 :
                       SWS_CS_ITU709;

    const int* src_coeff = sws_getCoefficients(src_cs);
    const int* dst_coeff = sws_getCoefficients(dst_cs);

    // Full range (1 = limited→full disabled, i.e. keep full range).
    // The uint8 RGBA intermediate buffer is already full-range 0–255.
    // For YUV formats (YUV420P, NV12) we output limited range (0).
    const int dst_range = (params.dst_fmt == AV_PIX_FMT_RGB24) ? 1 : 0;
    sws_setColorspaceDetails(ctx, src_coeff, /*srcRange=*/1,
                                   dst_coeff, dst_range,
                                   /*brightness=*/0, /*contrast=*/1 << 16,
                                   /*saturation=*/1 << 16);

    return ctx;
}

// ============================================================================
//  Pre-allocated RGBA8 staging buffer — reused across frames to avoid
//  a per-frame heap allocation (the main bottleneck in the old code).
// ============================================================================

static std::vector<uint8_t>& rgba8_staging(size_t min_bytes) {
    static std::vector<uint8_t> buf;
    if (buf.size() < min_bytes)
        buf.resize(min_bytes);
    return buf;
}

// ============================================================================
//  Helpers
// ============================================================================

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

// Convert a float Framebuffer to uint8_t RGBA8888 (bytes R, G, B, A).
// Writes into the caller-provided buffer (must be ≥ width * height * 4 bytes).
// Uses tbb::parallel_for for parallelism, with an sRGB gamma LUT when requested.
//
// Input stride is read dynamically from src.allocated_width() so the
// conversion respects the framebuffer's internal stride (cache-line aligned).
static void convert_fb_to_rgba8(const Framebuffer& src, int width, int height,
                                 bool apply_gamma, uint8_t* rgba8 /* OUT */) noexcept {
    const Color* src_data = src.data();
    const int   alloc_w   = src.allocated_width();

    tbb::parallel_for(0, height, [&](int y) {
        const Color* src_row = src_data + static_cast<size_t>(y) * alloc_w;
        uint8_t*     dst_row = rgba8      + static_cast<size_t>(y) * width * 4;

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
    });
}

// ============================================================================
//  Per-format conversion helpers
// ============================================================================

/// Shared implementation for all formats:
///   1. Convert float → uint8 RGBA (with sRGB gamma) into the staging buffer.
///   2. Run sws_scale to convert RGBA → target format into caller dst planes.
///
/// Source stride: the staging buffer is tightly packed (width × 4 bytes/row).
/// Destination stride: fixed to the native video width per the encoder contract.
static ConvertFrameResult convert_rgba_to_target(const ConvertFrameRequest& req,
                                                   AVPixelFormat dst_avfmt) {
    const uint64_t t0 = now_ns();

    // 1. Float → uint8 RGBA into the pre-allocated staging buffer.
    const size_t rgba_bytes = static_cast<size_t>(req.width) * req.height * 4;
    auto&        staging    = rgba8_staging(rgba_bytes);
    convert_fb_to_rgba8(req.src, req.width, req.height, req.apply_gamma, staging.data());

    // 2. sws_scale: uint8 RGBA → target format.
    const SwsParams params{
        .src_w = req.width, .src_h = req.height,
        .src_fmt = AV_PIX_FMT_RGBA,
        .dst_w = req.width, .dst_h = req.height,
        .dst_fmt = dst_avfmt,
        .color_matrix = req.color_matrix,
    };

    SwsContext* ctx = get_or_create_sws_context(params);
    if (!ctx)
        return ConvertFrameResult{.success = false};

    // Staging buffer is tightly packed RGBA8 → stride = width × 4.
    uint8_t* src_data[4]  = { staging.data(), nullptr, nullptr, nullptr };
    int      src_stride[4] = { req.width * 4, 0, 0, 0 };

    // Build destination plane pointers with fixed native-video strides.
    uint8_t* dst_data[4]  = { nullptr, nullptr, nullptr, nullptr };
    int      dst_stride[4] = { 0, 0, 0, 0 };

    switch (req.format) {
        case EncoderPixelFormat::YUV420P:
            if (!req.dst_y || !req.dst_u || !req.dst_v)
                throw std::invalid_argument("convert_frame(YUV420P): dst_y/u/v must not be null");
            dst_data[0]  = req.dst_y;
            dst_data[1]  = req.dst_u;
            dst_data[2]  = req.dst_v;
            // Output stride locked to native video width (tight planar)
            dst_stride[0] = req.dst_stride_y ? req.dst_stride_y : req.width;
            dst_stride[1] = req.dst_stride_u ? req.dst_stride_u : (req.width / 2);
            dst_stride[2] = req.dst_stride_v ? req.dst_stride_v : (req.width / 2);
            break;

        case EncoderPixelFormat::NV12:
            if (!req.dst_y || !req.dst_uv)
                throw std::invalid_argument("convert_frame(NV12): dst_y/uv must not be null");
            dst_data[0]  = req.dst_y;
            dst_data[1]  = req.dst_uv;   // NV12 UV plane is passed as plane 1
            dst_stride[0] = req.dst_stride_y ? req.dst_stride_y : req.width;
            dst_stride[1] = req.dst_stride_uv ? req.dst_stride_uv : req.width;
            break;

        case EncoderPixelFormat::RGB24:
            if (!req.dst_y)
                throw std::invalid_argument("convert_frame(RGB24): dst_y must not be null");
            dst_data[0]  = req.dst_y;
            dst_stride[0] = req.dst_stride_y ? req.dst_stride_y : req.width * 3;
            break;
    }

    const int ret = sws_scale(ctx, src_data, src_stride,
                              0, req.height, dst_data, dst_stride);

    return ConvertFrameResult{
        .success       = (ret == req.height),
        .used_simd     = true,
        .conversion_ns = now_ns() - t0,
    };
}

// ── Format-specific entry points (thin wrappers) ────────────────────────────

static ConvertFrameResult convert_rgba_to_yuv420p(const ConvertFrameRequest& req) {
    if (req.width % 2 != 0 || req.height % 2 != 0)
        return ConvertFrameResult{.success = false};
    return convert_rgba_to_target(req, AV_PIX_FMT_YUV420P);
}

static ConvertFrameResult convert_rgba_to_nv12(const ConvertFrameRequest& req) {
    if (req.width % 2 != 0 || req.height % 2 != 0)
        return ConvertFrameResult{.success = false};
    return convert_rgba_to_target(req, AV_PIX_FMT_NV12);
}

static ConvertFrameResult convert_rgba_to_rgb24(const ConvertFrameRequest& req) {
    return convert_rgba_to_target(req, AV_PIX_FMT_RGB24);
}

// ============================================================================
//  Public API
// ============================================================================

ConvertFrameResult convert_frame(const ConvertFrameRequest& req) {
    // ── Fast path: direct float framebuffer → YUV/NV12 ───────────────────
    // Bypasses the RGBA8 staging + sws_scale pipeline entirely.
    // Uses TBB parallel_for with inline gamma LUT and BT.709/601 matrix.
    //
    // TODO(HWY): add HWY SIMD acceleration once the U/V matrix bug is fixed.
    // The dispatch in direct_yuv_converter.cpp currently skips HWY and uses
    // the scalar TBB path which is pixel-identical with sws_scale.
    if (req.format == EncoderPixelFormat::YUV420P ||
        req.format == EncoderPixelFormat::NV12) {
        DirectYuvRequest dreq{
            .src            = req.src,
            .dst_y          = req.dst_y,
            .dst_u          = req.dst_u,
            .dst_v          = req.dst_v,
            .dst_uv         = req.dst_uv,
            .dst_stride_y   = req.dst_stride_y,
            .dst_stride_u   = req.dst_stride_u,
            .dst_stride_v   = req.dst_stride_v,
            .dst_stride_uv  = req.dst_stride_uv,
            .width          = req.width,
            .height         = req.height,
            .format         = req.format,
            .apply_gamma    = req.apply_gamma,
            .color_matrix   = req.color_matrix,
        };
        auto direct = convert_framebuffer_to_yuv_direct(dreq);
        if (direct.success) {
            return ConvertFrameResult{
                .success       = true,
                .used_simd     = direct.used_simd,
                .conversion_ns = direct.conversion_ns,
            };
        }
        // Fall through to the RGBA8 + sws_scale path if direct conversion
        // fails (e.g., odd dimensions, null pointers).
    }

    switch (req.format) {
        case EncoderPixelFormat::YUV420P: return convert_rgba_to_yuv420p(req);
        case EncoderPixelFormat::NV12:    return convert_rgba_to_nv12(req);
        case EncoderPixelFormat::RGB24:   return convert_rgba_to_rgb24(req);
    }
    throw std::invalid_argument("convert_frame: unknown EncoderPixelFormat");
}

ConvertFrameResult convert_frame_tight(
    const Framebuffer& src,
    uint8_t* dst_y, uint8_t* dst_u, uint8_t* dst_v, uint8_t* dst_uv,
    int width, int height,
    EncoderPixelFormat format,
    bool apply_gamma)
{
    ConvertFrameRequest req{
        .src            = src,
        .dst_y          = dst_y,
        .dst_u          = dst_u,
        .dst_v          = dst_v,
        .dst_uv         = dst_uv,
        .dst_stride_y   = (format == EncoderPixelFormat::RGB24) ? (width * 3) : width,
        .dst_stride_uv  = width,          // NV12: interleaved pairs → full width
        .dst_stride_u   = width / 2,
        .dst_stride_v   = width / 2,
        .color_matrix   = 0,
        .width          = width,
        .height         = height,
        .format         = format,
        .apply_gamma    = apply_gamma,
    };
    return convert_frame(req);
}

} // namespace chronon3d::video
