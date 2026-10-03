#pragma once

// ---------------------------------------------------------------------------
// packed_backend.hpp — Float→X direct quantization backends (TBB-parallel).
//
// PR5:    Extracted RGBA8 path from frame_converter.cpp into its own TU.
// PR-LinuxFastDev: Added native YUV420P/NV12/RGB24 fallback so that
//                   NullConvertEncoder can work in builds without
//                   CHRONON3D_ENABLE_NATIVE_FFMPEG (avcodec/libswscale).
//                   Production builds with FFmpeg continue to use swscale.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace chronon3d { class Framebuffer; }

namespace chronon3d::video {

struct ConvertFrameRequest;
struct ConvertFrameResult;

}  // namespace chronon3d::video

namespace chronon3d::video::packed {

/// Convert float Framebuffer pixels to 8-bit RGBA (R-G-B-A interleaved).
/// Uses TBB parallel_for internally. `apply_gamma` enables an OETF and
/// `rec709_oetf` selects BT.709 instead of the default sRGB curve.
void convert_fb_to_rgba8(const Framebuffer& src,
                         int width, int height,
                         bool apply_gamma,
                         uint8_t* rgba8,
                         bool rec709_oetf = false);

/// Convert linear BT.709 framebuffer pixels to limited-range planar
/// YUV420P10LE using the requested output transfer and BT.2020 primaries.
ConvertFrameResult convert_yuv420p10(const ConvertFrameRequest& req);

/// Native (non-FFmpeg) YUV420P / NV12 / RGB24 fallback.
/// Always uses BT.709 limited-range coefficients; sufficient for unit tests
/// and for applications that do not require swscale-grade color science.
/// Writes directly to the destination planes specified by `req.planes`.
/// Caller must have validated `req` (validate_conversion_request).
void convert_frame_fallback(const ConvertFrameRequest& req);

}  // namespace chronon3d::video::packed
