#pragma once

// ---------------------------------------------------------------------------
// reference_yuv_converter.hpp — Single-threaded scalar YUV conversion oracle.
//
// PR4B: DirectYuvRequest / DirectYuvResult types are defined locally since
// the production direct_yuv_converter.hpp was removed.  The gamma function
// uses Color::linear_to_srgb8() instead of the deleted LUT.
//
// Single-thread (no TBB / parallel_for), intentionally simple, NOT compiled
// into production targets (lives under tests/).
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cstdint>

namespace chronon3d::video {

// ── Local subset types (no longer from direct_yuv_converter.hpp) ────────

struct ReferenceYuvRequest {
    const Framebuffer& src;
    FramePlanes planes;
    int width{0};
    int height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};
    YuvMatrix matrix{YuvMatrix::BT709};
    bool apply_gamma{true};
};

struct ReferenceYuvResult {
    bool success{false};
    ConversionError error{ConversionError::None};
};

/// Single-threaded scalar reference: YUV420P.
ReferenceYuvResult reference_convert_to_yuv420p(const ReferenceYuvRequest& req);

/// Single-threaded scalar reference: NV12.
ReferenceYuvResult reference_convert_to_nv12(const ReferenceYuvRequest& req);

} // namespace chronon3d::video
