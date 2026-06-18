#pragma once

// ---------------------------------------------------------------------------
// reference_yuv_converter.hpp — Single-threaded scalar YUV conversion oracle.
//
// PR3: This is the ONLY scalar YUV implementation that survives after the
// production TBB fallback is removed.  It is:
//   - single-thread (no TBB / parallel_for)
//   - intentionally simple (readable, no staging tricks)
//   - NOT compiled into production targets (lives under tests/)
//   - used as a correctness oracle for frame sizes ≤ 256×256
//
// Callers:
//   tests/video/test_frame_converter_correctness.cpp
//   tests/bench/benchmark_frame_conversion.cpp (ScalarRef benchmark)
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/direct_yuv_converter.hpp>
#include <chronon3d/media/frame_conversion/direct_yuv_lut.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <algorithm>
#include <cstdint>

namespace chronon3d::video {

/// Single-threaded scalar reference: YUV420P.
/// Used only as correctness oracle — no SIMD, no parallelism.
DirectYuvResult reference_convert_to_yuv420p(const DirectYuvRequest& req);

/// Single-threaded scalar reference: NV12.
DirectYuvResult reference_convert_to_nv12(const DirectYuvRequest& req);

} // namespace chronon3d::video
