#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "utils/process_start.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/native_frame_importer_factory.hpp>
#include <chronon3d/media/video/detail/video_execution_legacy.hpp>
#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <cuda.h>
#endif

#include <spdlog/spdlog.h>
#include <filesystem>
#include <functional>
#include <cstdlib>
#include <memory>
#include <thread>

namespace chronon3d::cli {

#include "pipe_export_pipeline_support.inc"
#include "pipe_export_pipeline_setup.inc"
#include "pipe_export_pipeline_loop.inc"
#include "pipe_export_pipeline_warmup.inc"

} // namespace chronon3d::cli
