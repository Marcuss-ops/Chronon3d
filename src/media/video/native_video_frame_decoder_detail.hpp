// native_video_frame_decoder_detail.hpp — internal declarations shared by the
// native_video_frame_decoder_*.cpp translation units. Not installed and not
// part of any public API.

#pragma once

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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media {

// Defined in native_video_frame_decoder_helpers.cpp.
enum AVPixelFormat select_cuda_format(AVCodecContext*,
                                      const enum AVPixelFormat* formats);
std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws, std::vector<uint8_t>& rgba,
    RenderCounters* counters, bool enable_swscale = true);
bool build_source_sample_table(AVFormatContext* fmt, int stream_index,
                               SourceSampleTable& out);
void set_decode_diagnostic(DecodeDiagnostic* out,
                           DecodeFailureReason reason,
                           int ffmpeg_error,
                           std::int64_t pts,
                           std::int64_t dts,
                           std::uint64_t source_order,
                           std::string message);

} // namespace chronon3d::media

#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG
