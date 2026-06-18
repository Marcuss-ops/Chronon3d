#pragma once

// ---------------------------------------------------------------------------
// direct_yuv_converter.hpp — Direct float framebuffer → YUV/NV12 converter.
//
// PR1: DirectYuvRequest now accepts YuvMatrix / ColorRange directly
// instead of an integer color_matrix.  DirectYuvResult reports the
// concrete FrameConversionBackend (HighwayDirect or — after PR3 — Libyuv)
// and a ConversionError code on failure.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <cstdint>

namespace chronon3d::video {

/// Parameters for the direct float→YUV conversion path.  Mirrors the
/// subset of ConvertFrameRequest needed by the float-direct kernels.
struct DirectYuvRequest {
    const Framebuffer& src;
    FramePlanes planes;

    int width{0};
    int height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};

    YuvMatrix matrix{YuvMatrix::BT709};
    ColorRange range{ColorRange::Limited};
    bool apply_gamma{true};
};

struct DirectYuvResult {
    bool success{false};
    FrameConversionBackend backend{FrameConversionBackend::Unavailable};
    ConversionError error{ConversionError::None};
    uint64_t conversion_ns{0};
};

/// Convert a float framebuffer directly to YUV420P or NV12.  Reports the
/// concrete backend via the result.  BT.2020 is rejected with
/// `error = UnsupportedMatrix`; callers should fall back to swscale.
DirectYuvResult convert_framebuffer_to_yuv_direct(const DirectYuvRequest& req);

DirectYuvResult convert_to_yuv420p_hwy(const DirectYuvRequest& req);
DirectYuvResult convert_to_nv12_hwy(const DirectYuvRequest& req);
DirectYuvResult convert_to_yuv420p_parallel(const DirectYuvRequest& req);
DirectYuvResult convert_to_nv12_parallel(const DirectYuvRequest& req);

} // namespace chronon3d::video
