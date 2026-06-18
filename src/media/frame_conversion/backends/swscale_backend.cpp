// ============================================================================
// swscale_backend.cpp — FFmpeg libswscale YUV/RGB24 conversion backend.
//
// PR5: Moved from frame_converter.cpp.  Takes explicit scratch buffer via
// the `scratch` parameter — the caller (selector) owns the RGBA8 staging
// buffer lifecycle.  Internally calls packed::convert_fb_to_rgba8 to produce
// the RGBA8 input for sws_scale().
// ============================================================================

#include <chronon3d/media/frame_conversion/backends/swscale_backend.hpp>
#include <chronon3d/media/frame_conversion/backends/packed_backend.hpp>

#include <chronon3d/core/profiling/profiling.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}
#endif

#include <memory>

namespace chronon3d::video::swscale {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

// ── Swscale context management ──────────────────────────────────────────

struct SwsParams {
    int          src_w, src_h;
    AVPixelFormat src_fmt;
    int          dst_w, dst_h;
    AVPixelFormat dst_fmt;
    YuvMatrix     matrix;
    ColorRange    range;
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
    if (!ctx) return nullptr;
    ctx_owner.reset(ctx);

    const int src_cs = (params.matrix == YuvMatrix::BT601)  ? SWS_CS_DEFAULT :
                       (params.matrix == YuvMatrix::BT2020) ? SWS_CS_BT2020  :
                                                                SWS_CS_ITU709;
    const int dst_cs = src_cs;

    const int* src_coeff = sws_getCoefficients(src_cs);
    const int* dst_coeff = sws_getCoefficients(dst_cs);
    const int dst_range = (params.dst_fmt == AV_PIX_FMT_RGB24 ||
                           params.range == ColorRange::Full) ? 1 : 0;
    sws_setColorspaceDetails(ctx, src_coeff, 1, dst_coeff, dst_range, 0, 1 << 16, 1 << 16);
    return ctx;
}

// ── Core swscale dispatch ───────────────────────────────────────────────

static ConvertFrameResult dispatch(const ConvertFrameRequest& req,
                                   AVPixelFormat dst_avfmt,
                                   std::vector<uint8_t>& scratch)
{
    const uint64_t t0 = profiling::timestamp_ns();

    // Ensure scratch is large enough for RGBA8 staging.
    const std::size_t rgba_bytes = static_cast<std::size_t>(req.width) * req.height * 4;
    if (scratch.size() < rgba_bytes) scratch.resize(rgba_bytes);

    // Produce RGBA8 staging buffer (float→uint8 via packed backend).
    packed::convert_fb_to_rgba8(req.src, req.width, req.height, req.apply_gamma,
                                scratch.data());

    const SwsParams params{
        req.width, req.height, AV_PIX_FMT_RGBA,
        req.width, req.height, dst_avfmt,
        req.matrix, req.range,
    };
    SwsContext* ctx = get_or_create_sws_context(params);
    if (!ctx) {
        return ConvertFrameResult{
            .success = false,
            .backend = FrameConversionBackend::Swscale,
            .error = ConversionError::BackendError,
        };
    }

    uint8_t* src_data[4] = { scratch.data(), nullptr, nullptr, nullptr };
    int src_stride[4] = { req.width * 4, 0, 0, 0 };
    uint8_t* dst_data[4] = { nullptr, nullptr, nullptr, nullptr };
    int dst_stride[4] = { 0, 0, 0, 0 };

    switch (req.format) {
        case EncoderPixelFormat::YUV420P:
            dst_data[0] = req.planes.y; dst_data[1] = req.planes.u; dst_data[2] = req.planes.v;
            dst_stride[0] = req.planes.stride_y ? req.planes.stride_y : req.width;
            dst_stride[1] = req.planes.stride_u ? req.planes.stride_u : (req.width / 2);
            dst_stride[2] = req.planes.stride_v ? req.planes.stride_v : (req.width / 2);
            break;
        case EncoderPixelFormat::NV12:
            dst_data[0] = req.planes.y;  dst_data[1] = req.planes.uv;
            dst_stride[0] = req.planes.stride_y  ? req.planes.stride_y  : req.width;
            dst_stride[1] = req.planes.stride_uv ? req.planes.stride_uv : req.width;
            break;
        case EncoderPixelFormat::RGB24:
            dst_data[0] = req.planes.y;
            dst_stride[0] = req.planes.stride_y ? req.planes.stride_y : (req.width * 3);
            break;
        case EncoderPixelFormat::RGBA8:   // unreachable — RGBA8 routes to Packed
            break;
    }
    const int ret = sws_scale(ctx, src_data, src_stride, 0, req.height, dst_data, dst_stride);
    return ConvertFrameResult{
        .success = (ret == req.height),
        .backend = FrameConversionBackend::Swscale,
        .error = (ret == req.height) ? ConversionError::None : ConversionError::BackendError,
        .conversion_ns = profiling::timestamp_ns() - t0,
    };
}

#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG

// ── Public entry point ──────────────────────────────────────────────────

ConvertFrameResult convert_frame_to_yuv(const ConvertFrameRequest& req,
                                        std::vector<uint8_t>& scratch)
{
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    // Validate YUV dimension constraints.
    const bool is_yuv = (req.format == EncoderPixelFormat::YUV420P ||
                         req.format == EncoderPixelFormat::NV12);
    if (is_yuv && ((req.width % 2) != 0 || (req.height % 2) != 0)) {
        return ConvertFrameResult{
            .success = false,
            .backend = FrameConversionBackend::Swscale,
            .error = ConversionError::OddDims,
        };
    }

    switch (req.format) {
        case EncoderPixelFormat::YUV420P: return dispatch(req, AV_PIX_FMT_YUV420P, scratch);
        case EncoderPixelFormat::NV12:    return dispatch(req, AV_PIX_FMT_NV12, scratch);
        case EncoderPixelFormat::RGB24:   return dispatch(req, AV_PIX_FMT_RGB24, scratch);
        default:
            return ConvertFrameResult{
                .success = false,
                .backend = FrameConversionBackend::Swscale,
                .error = ConversionError::UnsupportedFormat,
            };
    }
#else
    (void)req;
    (void)scratch;
    return ConvertFrameResult{
        .success = false,
        .backend = FrameConversionBackend::Unavailable,
        .error = ConversionError::BackendError,
    };
#endif
}

}  // namespace chronon3d::video::swscale
