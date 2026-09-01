#include "native_av_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#endif
#include <cuda.h>
#include <libavutil/hwcontext_cuda.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool set_codec_option_checked(AVCodecContext* codec, const char* key,
                              const std::string& value) {
    const int rc = av_opt_set(codec, key, value.c_str(), AV_OPT_SEARCH_CHILDREN);
    if (rc < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(rc, error, sizeof(error));
        spdlog::error("[native_av] unsupported encoder option {}='{}': {}",
                      key, value, error);
        return false;
    }
    return true;
}
}

namespace chronon3d::cli {

#include "native_av_encoder_open.inc"
#include "native_av_encoder_lifecycle.inc"
#include "native_av_encoder_cuda_queue.inc"
#include "native_av_encoder_direct_yuv.inc"
#include "native_av_encoder_native_surface.inc"

} // namespace chronon3d::cli
