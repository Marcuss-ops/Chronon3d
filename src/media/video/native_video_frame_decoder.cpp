// media/video/native_video_frame_decoder.cpp — see the header for the
// contract. Decoding is sequential-friendly: the render loop walks frames in
// order, so after a keyframe-aligned seek the decoder counts output frames to
// the target index and caches the result (bounded).
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

#include <chronon3d/math/color.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
extern "C" {
#include <libavutil/hwcontext_cuda.h>
}
#endif
#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace chronon3d::media {
#include "native_video_frame_decoder_convert.inc"
#include "native_video_frame_decoder_session.inc"
#include "native_video_frame_decoder_core.inc"
#include "native_video_frame_decoder_api.inc"
} // namespace chronon3d::media

#endif
