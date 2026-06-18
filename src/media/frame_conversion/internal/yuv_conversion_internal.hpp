#pragma once

// ---------------------------------------------------------------------------
// yuv_conversion_internal.hpp — Internal declarations for the YUV
// conversion paths: Highway SIMD and sws_scale.
//
// This header is NOT part of the public API.  It exists solely so the
// conversion benchmark can call each path independently and compare
// timing + output correctness.
//
// PR3: Scalar/TBB fallback (convert_to_yuv420p_parallel / _nv12_parallel)
// removed from production.  Reference scalar lives in
// tests/video/reference_yuv_converter.hpp for test-only oracle use.
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/direct_yuv_converter.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <cstdint>

namespace chronon3d::video {

// ── Highway SIMD path (defined in direct_yuv_converter_hwy.cpp) ─────────
DirectYuvResult convert_to_yuv420p_hwy(const DirectYuvRequest& req);
DirectYuvResult convert_to_nv12_hwy(const DirectYuvRequest& req);

// ── sws_scale path (defined in frame_converter.cpp) ─────────────────────
// Converts float Framebuffer → uint8 RGBA8 staging → sws_scale → YUV/NV12.
// Returns the ConvertFrameResult with success flag and conversion_ns.
ConvertFrameResult convert_rgba_to_yuv420p_swscale(const ConvertFrameRequest& req);
ConvertFrameResult convert_rgba_to_nv12_swscale(const ConvertFrameRequest& req);

} // namespace chronon3d::video
