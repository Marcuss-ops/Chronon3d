#include "pipe_export_session.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif
#include "temporal_render_bridge.hpp"
#include <spdlog/spdlog.h>

#include <optional>
#include <cstring>
#include <span>
#include <thread>

namespace chronon3d::cli {

#include "pipe_export_session_profile.inc"
#include "pipe_export_session_stages.inc"
#include "src/media/video/direct_yuv/pipe_export_session_direct_yuv.inc"
#include "pipe_export_session_render_loop.inc"

} // namespace chronon3d::cli