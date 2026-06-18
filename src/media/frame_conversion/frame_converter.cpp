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
            return ConvertFrameResult{
                .success = false,
                .backend = FrameConversionBackend::Swscale,
                .error = ConversionError::BackendError,
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

} // namespace chronon3d::video
