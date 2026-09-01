#include "daemon_service.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif
#include "../utils/job/cli_render_utils.hpp"

#include "utils/common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <array>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {

#include "daemon_service_bootstrap.inc"
#include "daemon_service_commands.inc"
#include "daemon_service_ipc.inc"

} // namespace chronon3d::cli
