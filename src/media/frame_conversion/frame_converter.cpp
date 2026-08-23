// ============================================================================
// frame_converter.cpp — Central selector + dispatcher with scratch ownership.
//
// PR5: Backend implementations moved to backends/{packed,swscale}_backend.cpp.
// This file retains only selection logic, validation, and the dispatcher.
// The RGBA8 scratch buffer for swscale staging is owned here (thread_local)
// and passed explicitly to the swscale backend.
// ============================================================================

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/backends/packed_backend.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include <chronon3d/media/frame_conversion/backends/swscale_backend.hpp>
#endif

#include <chronon3d/core/profiling/profiling.hpp>
#include <cstdint>
#include <vector>

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
    // PR4B: All YUV420P/NV12/RGB24/BT.2020 route through swscale.  When the
    // build disables CHRONON3D_ENABLE_NATIVE_FFMPEG, caps.swscale is false;
    // the dispatcher still returns Swscale here so the Swscale case can
    // transparently fall back to packed::convert_frame_fallback().
    (void)caps;
    return FrameConversionBackend::Swscale;
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
            packed::convert_fb_to_rgba8(req.src, req.width, req.height,
                                       req.apply_gamma, req.planes.y);
            return ConvertFrameResult{
                .success = true,
                .backend = FrameConversionBackend::Packed,
                .error = ConversionError::None,
                .conversion_ns = profiling::timestamp_ns() - t0,
            };
        }
        case FrameConversionBackend::HighwayDirect:
        case FrameConversionBackend::Swscale: {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
            // Explicit scratch ownership: selector creates and owns the
            // RGBA8 staging buffer, passes it to the swscale backend.
            thread_local std::vector<uint8_t> scratch;
            return swscale::convert_frame_to_yuv(req, scratch);
#else
            // Native (non-FFmpeg) fallback: produce valid YUV/RGB24 bytes
            // using BT.709 limited-range quantization directly to the
            // destination planes.  Sufficient for unit tests and for
            // applications that do not require swscale-grade color science.
            const uint64_t t0 = profiling::timestamp_ns();
            packed::convert_frame_fallback(req);
            return ConvertFrameResult{
                .success = true,
                .backend = FrameConversionBackend::Swscale,
                .error = ConversionError::None,
                .conversion_ns = profiling::timestamp_ns() - t0,
            };
#endif
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

ConvertFrameResult composite_overlay_nv12(const CompositeOverlayNv12Request& req) {
    const uint64_t t0 = profiling::timestamp_ns();
    if (req.width <= 0 || req.height <= 0 || (req.width % 2 != 0) || (req.height % 2 != 0)) {
        return ConvertFrameResult{.success = false, .backend = FrameConversionBackend::Packed, .error = ConversionError::OddDims};
    }
    if (!req.bg_planes.y || !req.bg_planes.uv || !req.out_planes.y || !req.out_planes.uv) {
        return ConvertFrameResult{.success = false, .backend = FrameConversionBackend::Packed, .error = ConversionError::NullPointer};
    }

    const int width = req.width;
    const int height = req.height;
    const int fg_stride = req.fg_src.allocated_width();
    const Color* fg_pixels = req.fg_src.data();

    const int stride_bg_y = req.bg_planes.stride_y;
    const int stride_bg_uv = req.bg_planes.stride_uv;
    const int stride_out_y = req.out_planes.stride_y;
    const int stride_out_uv = req.out_planes.stride_uv;

    for (int y2 = 0; y2 < height; y2 += 2) {
        const uint8_t* bg_y0 = req.bg_planes.y + y2 * stride_bg_y;
        const uint8_t* bg_y1 = req.bg_planes.y + (y2 + 1) * stride_bg_y;
        uint8_t* out_y0 = req.out_planes.y + y2 * stride_out_y;
        uint8_t* out_y1 = req.out_planes.y + (y2 + 1) * stride_out_y;

        const uint8_t* bg_uv = req.bg_planes.uv + (y2 / 2) * stride_bg_uv;
        uint8_t* out_uv = req.out_planes.uv + (y2 / 2) * stride_out_uv;

        const Color* fg_row0 = fg_pixels + y2 * fg_stride;
        const Color* fg_row1 = fg_pixels + (y2 + 1) * fg_stride;

        for (int x2 = 0; x2 < width; x2 += 2) {
            const Color& c00 = fg_row0[x2];
            const Color& c10 = fg_row0[x2 + 1];
            const Color& c01 = fg_row1[x2];
            const Color& c11 = fg_row1[x2 + 1];

            const float a00 = std::clamp(c00.a, 0.0f, 1.0f);
            const float a10 = std::clamp(c10.a, 0.0f, 1.0f);
            const float a01 = std::clamp(c01.a, 0.0f, 1.0f);
            const float a11 = std::clamp(c11.a, 0.0f, 1.0f);

            // Fast path: if entire 2x2 block is transparent, copy Y and UV
            if (a00 == 0.0f && a10 == 0.0f && a01 == 0.0f && a11 == 0.0f) {
                out_y0[x2]     = bg_y0[x2];
                out_y0[x2 + 1] = bg_y0[x2 + 1];
                out_y1[x2]     = bg_y1[x2];
                out_y1[x2 + 1] = bg_y1[x2 + 1];
                out_uv[x2]     = bg_uv[x2];
                out_uv[x2 + 1] = bg_uv[x2 + 1];
                continue;
            }

            auto blend_y = [](uint8_t bg_val, const Color& c, float a) -> uint8_t {
                if (a <= 0.0f) return bg_val;
                const float y_fg = 16.0f + 219.0f * (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b);
                const float mixed = (1.0f - a) * static_cast<float>(bg_val) + a * y_fg;
                return static_cast<uint8_t>(std::clamp(std::round(mixed), 16.0f, 235.0f));
            };

            out_y0[x2]     = blend_y(bg_y0[x2], c00, a00);
            out_y0[x2 + 1] = blend_y(bg_y0[x2 + 1], c10, a10);
            out_y1[x2]     = blend_y(bg_y1[x2], c01, a01);
            out_y1[x2 + 1] = blend_y(bg_y1[x2 + 1], c11, a11);

            const float a_block = (a00 + a10 + a01 + a11) * 0.25f;
            if (a_block <= 0.0f) {
                out_uv[x2]     = bg_uv[x2];
                out_uv[x2 + 1] = bg_uv[x2 + 1];
            } else {
                const float a_sum = a00 + a10 + a01 + a11;
                const float r_avg = (c00.r * a00 + c10.r * a10 + c01.r * a01 + c11.r * a11) / a_sum;
                const float g_avg = (c00.g * a00 + c10.g * a10 + c01.g * a01 + c11.g * a11) / a_sum;
                const float b_avg = (c00.b * a00 + c10.b * a10 + c01.b * a01 + c11.b * a11) / a_sum;

                const float u_fg = 128.0f + 224.0f * (-0.1146f * r_avg - 0.3854f * g_avg + 0.5000f * b_avg);
                const float v_fg = 128.0f + 224.0f * ( 0.5000f * r_avg - 0.4542f * g_avg - 0.0458f * b_avg);

                const float bg_u = static_cast<float>(bg_uv[x2]);
                const float bg_v = static_cast<float>(bg_uv[x2 + 1]);

                const float u_out = (1.0f - a_block) * bg_u + a_block * u_fg;
                const float v_out = (1.0f - a_block) * bg_v + a_block * v_fg;

                out_uv[x2]     = static_cast<uint8_t>(std::clamp(std::round(u_out), 16.0f, 240.0f));
                out_uv[x2 + 1] = static_cast<uint8_t>(std::clamp(std::round(v_out), 16.0f, 240.0f));
            }
        }
    }

    return ConvertFrameResult{
        .success = true,
        .backend = FrameConversionBackend::Packed,
        .error = ConversionError::None,
        .conversion_ns = profiling::timestamp_ns() - t0,
    };
}

} // namespace chronon3d::video
