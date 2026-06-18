#pragma once

// ---------------------------------------------------------------------------
// swscale_backend.hpp — FFmpeg libswscale YUV/RGB24 conversion backend.
//
// PR5: Extracted from frame_converter.cpp.  Takes explicit scratch buffer
// ownership via the `scratch` parameter — the caller (selector) creates,
// owns, and reuses the RGBA8 staging buffer across iterations.
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <vector>

namespace chronon3d::video::swscale {

/// Convert a float framebuffer to YUV420P / NV12 / RGB24 via swscale.
///
/// @param req      The conversion request.
/// @param scratch  RGBA8 staging buffer, owned by the caller.  Will be resized
///                 to at least `width * height * 4` bytes if needed.
/// @return ConvertFrameResult with success, backend, error, and timing.
ConvertFrameResult convert_frame_to_yuv(const ConvertFrameRequest& req,
                                        std::vector<uint8_t>& scratch);

}  // namespace chronon3d::video::swscale
