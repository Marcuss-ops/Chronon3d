#pragma once

// ---------------------------------------------------------------------------
// direct_yuv_converter.hpp — Direct float framebuffer → YUV/NV12 converter.
//
// Bypasses the two-pass pipeline (float → RGBA8 staging → sws_scale → YUV)
// by computing YUV pixels directly from the linear float framebuffer using
// a gamma LUT, BT.709/601 matrix, and 4:2:0 chroma subsampling.
//
// Design:
//  - Scalar baseline first (pixel-identical reference).
//  - Highway SIMD fast-path can be added later as a drop-in replacement.
//  - Callers must ensure width/height are even (4:2:0 requirement).
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/video/frame_converter.hpp>
#include <cstdint>

namespace chronon3d::video {

/// Parameters for a direct float-to-YUV conversion request.
/// All output pointers must be non-null for the target format.
struct DirectYuvRequest {
    const Framebuffer& src;

    // Output planes (format-dependent):
    //   YUV420P: dst_y, dst_u, dst_v
    //   NV12:    dst_y, dst_uv
    uint8_t* dst_y{nullptr};
    uint8_t* dst_u{nullptr};    // YUV420P only
    uint8_t* dst_v{nullptr};    // YUV420P only
    uint8_t* dst_uv{nullptr};   // NV12 only

    int dst_stride_y{0};
    int dst_stride_u{0};
    int dst_stride_v{0};
    int dst_stride_uv{0};

    int         width{0};
    int         height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};
    bool        apply_gamma{true};
    int         color_matrix{0};  // 0=BT.709, 1=BT.601
};

struct DirectYuvResult {
    bool        success{false};
    bool        used_simd{false};
    uint64_t    conversion_ns{0};
};

/// Convert a float framebuffer directly to YUV420P or NV12.
/// Returns false if dimensions are odd or pointers are null.
DirectYuvResult convert_framebuffer_to_yuv_direct(const DirectYuvRequest& req);

} // namespace chronon3d::video
