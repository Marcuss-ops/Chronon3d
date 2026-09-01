#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "pipe_timing_sidecar.hpp"
#include "../../../utils/process_start.hpp"
#include "../../../utils/telemetry/nvml_sampler.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/frame_timing_summary.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif

namespace chronon3d::cli {

PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes,
    runtime::DeviceScheduler* device_scheduler,
    std::shared_ptr<media::VideoJobExecutionContext> execution)
{
#include "video_export_pipe_setup.inc"
#include "video_export_pipe_timing.inc"
#include "video_export_pipe_counters.inc"
#include "video_export_pipe_finalize.inc"
}

} // namespace chronon3d::cli
