#include <chronon3d/media/frame_conversion/frame_converter.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/parallel_for.h>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace chronon3d::video {

// ============================================================================
//  Capability advertisement — compile-time-resolved once, cached.
// ============================================================================

const FrameConversionCapabilities& frame_conversion_capabilities() {
    static const FrameConversionCapabilities caps{
        .highway_direct = false,  // PR4B: Highway removed
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
        .swscale = true,
#else
        .swscale = false,
#endif
        .highway_direct_matrices = 0,
    };
    return caps;
}

// ============================================================================
//  Pure error / capability helpers
// ============================================================================

const char* conversion_error_to_string(ConversionError err) {
    switch (err) {
        case ConversionError::None:              return "None";
        case ConversionError::OddDims:           return "Dimensions must be even for 4:2:0 YUV";
        case ConversionError::NullPointer:       return "Required plane pointers are null";
        case ConversionError::UnsupportedMatrix: return "Color matrix not supported by selected backend";
        case ConversionError::UnsupportedRange:  return "Color range not supported by selected backend";
        case ConversionError::UnsupportedFormat: return "Target EncoderPixelFormat not handled";
        case ConversionError::BackendError:      return "Conversion backend runtime failure";
    }
    return "Unknown";
}

FrameConversionBackend select_backend(const ConvertFrameRequest& req) {
    const auto& caps = frame_conversion_capabilities();

    if (req.format == EncoderPixelFormat::RGBA8) {
        return FrameConversionBackend::Packed;
    }
    // PR4B: All YUV420P/NV12/RGB24/BT.2020 route through swscale.
    if (caps.swscale) {
        return FrameConversionBackend::Swscale;
    }
    return FrameConversionBackend::Unavailable;
}

ConversionError validate_conversion_request(const ConvertFrameRequest& req) {
    if (req.width <= 0 || req.height <= 0) return ConversionError::OddDims;
    const bool is_yuv = (req.format == EncoderPixelFormat::YUV420P ||
                         req.format == EncoderPixelFormat::NV12);
    if (is_yuv && ((req.width % 2) != 0 || (req.height % 2) != 0)) {
        return ConversionError::OddDims;
    }
    if (is_yuv) {
        if (!req.planes.y)                                 return ConversionError::NullPointer;
        if (req.format == EncoderPixelFormat::YUV420P &&
            (!req.planes.u || !req.planes.v))              return ConversionError::NullPointer;
        if (req.format == EncoderPixelFormat::NV12 &&
            !req.planes.uv)                                return ConversionError::NullPointer;
    }
    if (req.format == EncoderPixelFormat::RGB24 && !req.planes.y) {
        return ConversionError::NullPointer;
    }
    // Range: full range is supported by Swscale.
    // (No backend-specific reject for Limited/Full needed today.)
    return ConversionError::None;
}

std::optional<FramePlanes> resolve_frame_planes(
    uint8_t* packed_buffer, std::size_t packed_size,
    int width, int height, EncoderPixelFormat format)
{
    if (!packed_buffer || width <= 0 || height <= 0) {
        return std::nullopt;
    }
    switch (format) {
        case EncoderPixelFormat::YUV420P: {
            const std::size_t need =
                static_cast<std::size_t>(width) * height +
                static_cast<std::size_t>(width) * height / 2;
            if (packed_size < need) return std::nullopt;
            return FramePlanes{
                .y = packed_buffer,
                .u = packed_buffer + static_cast<std::size_t>(width) * height,
                .v = packed_buffer + static_cast<std::size_t>(width) * height * 5 / 4,
                .stride_y = width,
                .stride_u = width / 2,
                .stride_v = width / 2,
            };
        }
        case EncoderPixelFormat::NV12: {
            const std::size_t need = static_cast<std::size_t>(width) * height * 3 / 2;
            if (packed_size < need) return std::nullopt;
            return FramePlanes{
                .y = packed_buffer,
                .uv = packed_buffer + static_cast<std::size_t>(width) * height,
                .stride_y = width,
                .stride_uv = width,
            };
        }
        case EncoderPixelFormat::RGB24: {
            const std::size_t need = static_cast<std::size_t>(width) * height * 3;
            if (packed_size < need) return std::nullopt;
            return FramePlanes{
                .y = packed_buffer,
                .stride_y = width * 3,
            };
        }
        case EncoderPixelFormat::RGBA8: {
            const std::size_t need = static_cast<std::size_t>(width) * height * 4;
            if (packed_size < need) return std::nullopt;
            return FramePlanes{
                .y = packed_buffer,
                .stride_y = width * 4,
            };
        }
    }
    return std::nullopt;
}

// ============================================================================
//  Packed RGBA8 path (TBB-scalar float→uint8 quantization)
// ============================================================================

static std::vector<uint8_t>& rgba8_staging(std::size_t min_bytes) {
    thread_local std::vector<uint8_t> buf;
    if (buf.size() < min_bytes) buf.resize(min_bytes);
    return buf;
}

void convert_fb_to_rgba8_public(const Framebuffer& src, int width, int height, bool apply_gamma, uint8_t* rgba8) {
    const Color* src_data = src.data();
    const int alloc_w = src.allocated_width();
    const int grain = std::max(32, height / 16);
    parallel_for_tracked(tbb::blocked_range<int>(0, height, grain), [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            const Color* src_row = src_data + static_cast<std::size_t>(y) * alloc_w;
            uint8_t* dst_row = rgba8 + static_cast<std::size_t>(y) * width * 4;
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

// ============================================================================
//  Swscale backend
// ============================================================================

// Forward declarations for the static Swscale helpers below.  Required so
// the dispatcher in convert_frame() can call swscale_dispatch_rgb24() and
// swscale_dispatch() before their definitions appear further down.
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
static ConvertFrameResult swscale_dispatch(const ConvertFrameRequest& req, AVPixelFormat dst_avfmt);
static ConvertFrameResult swscale_dispatch_rgb24(const ConvertFrameRequest& req);
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
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
    ctx_owner.reset(ctx);  // takes ownership (frees old ctx if reallocated)

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

static ConvertFrameResult swscale_dispatch(const ConvertFrameRequest& req, AVPixelFormat dst_avfmt) {
    const uint64_t t0 = profiling::timestamp_ns();
    const std::size_t rgba_bytes = static_cast<std::size_t>(req.width) * req.height * 4;
    auto& staging = rgba8_staging(rgba_bytes);
    convert_fb_to_rgba8_public(req.src, req.width, req.height, req.apply_gamma, staging.data());

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

    uint8_t* src_data[4] = { staging.data(), nullptr, nullptr, nullptr };
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
        case EncoderPixelFormat::RGBA8:   // pragma unreachable — RGBA8 routes to Packed backend
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

/// Thin wrapper: RGB24 output via swscale (AV_PIX_FMT_RGB24).
static ConvertFrameResult swscale_dispatch_rgb24(const ConvertFrameRequest& req) {
    return swscale_dispatch(req, AV_PIX_FMT_RGB24);
}
#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG

ConvertFrameResult convert_rgba_to_yuv420p_swscale(const ConvertFrameRequest& req) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if ((req.width % 2) != 0 || (req.height % 2) != 0) {
        return ConvertFrameResult{
            .success = false,
            .backend = FrameConversionBackend::Swscale,
            .error = ConversionError::OddDims,
        };
    }
    return swscale_dispatch(req, AV_PIX_FMT_YUV420P);
#else
    return ConvertFrameResult{
        .success = false,
        .backend = FrameConversionBackend::Unavailable,
        .error = ConversionError::BackendError,
    };
#endif
}

ConvertFrameResult convert_rgba_to_nv12_swscale(const ConvertFrameRequest& req) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if ((req.width % 2) != 0 || (req.height % 2) != 0) {
        return ConvertFrameResult{
            .success = false,
            .backend = FrameConversionBackend::Swscale,
            .error = ConversionError::OddDims,
        };
    }
    return swscale_dispatch(req, AV_PIX_FMT_NV12);
#else
    return ConvertFrameResult{
        .success = false,
        .backend = FrameConversionBackend::Unavailable,
        .error = ConversionError::BackendError,
    };
#endif
}

// ============================================================================
//  Dispatcher
// ============================================================================

ConvertFrameResult convert_frame(const ConvertFrameRequest& req) {
    const ConversionError validation = validate_conversion_request(req);
    if (validation != ConversionError::None) {
        return ConvertFrameResult{
            .success = false,
            .backend = FrameConversionBackend::Unavailable,
            .error = validation,
        };
    }

    const FrameConversionBackend choice = select_backend(req);

    switch (choice) {
        case FrameConversionBackend::Packed: {
            const uint64_t t0 = profiling::timestamp_ns();
            convert_fb_to_rgba8_public(req.src, req.width, req.height, req.apply_gamma, req.planes.y);
            return ConvertFrameResult{
                .success = true,
                .backend = FrameConversionBackend::Packed,
                .error = ConversionError::None,
                .conversion_ns = profiling::timestamp_ns() - t0,
            };
        }
        case FrameConversionBackend::HighwayDirect:
        case FrameConversionBackend::Swscale: {
            // PR4B: Swscale is the canonical YUV/RGB backend.
            // HighwayDirect enum kept for API compatibility but never reached
            // (select_backend always returns Swscale for non-RGBA8).
            if (req.format == EncoderPixelFormat::RGB24) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
                return swscale_dispatch_rgb24(req);
#else
                return ConvertFrameResult{
                    .success = false,
                    .backend = FrameConversionBackend::Swscale,
                    .error = ConversionError::BackendError,
                };
#endif
            }
            if (req.format == EncoderPixelFormat::YUV420P) return convert_rgba_to_yuv420p_swscale(req);
            if (req.format == EncoderPixelFormat::NV12)    return convert_rgba_to_nv12_swscale(req);
            return ConvertFrameResult{
                .success = false,
                .backend = FrameConversionBackend::Swscale,
                .error = ConversionError::UnsupportedFormat,
            };
        }
        case FrameConversionBackend::Unavailable:
        default:
            return ConvertFrameResult{
                .success = false,
                .backend = FrameConversionBackend::Unavailable,
                .error = ConversionError::BackendError,
            };
    }
}



ConvertFrameResult convert_frame_tight(
    const Framebuffer& src, FramePlanes planes,
    int width, int height, EncoderPixelFormat format,
    YuvMatrix matrix, ColorRange range,
    bool apply_gamma)
{
    ConvertFrameRequest req{
        .src = src,
        .planes = planes,
        .width = width,
        .height = height,
        .format = format,
        .matrix = matrix,
        .range = range,
        .apply_gamma = apply_gamma,
    };
    return convert_frame(req);
}

} // namespace chronon3d::video
