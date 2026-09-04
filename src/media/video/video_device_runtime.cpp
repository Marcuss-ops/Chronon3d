#include <chronon3d/media/video/video_device_runtime.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <chronon3d/media/video/cuda_image_resource.hpp>
#endif

#include <spdlog/spdlog.h>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <libavutil/hwcontext_cuda.h>
#endif
}

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <string>
#include <utility>
#include <chrono>

#include "video_device_runtime_lifecycle_detail.hpp"
#include "video_device_runtime_surfaces_detail.hpp"
#include "video_device_runtime_cuda_cache_detail.hpp"
#include "video_device_runtime_registry_detail.hpp"
