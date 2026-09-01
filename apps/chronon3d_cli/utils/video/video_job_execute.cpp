#include "video_export_support.hpp"
#include "gop_smart_copy.hpp"
#include "../../commands/video/common/pipe_export_pipeline.hpp"
#include "../../commands/video/common/video_export_common.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/detail/video_execution_legacy.hpp>

#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <chrono>
#include <filesystem>
#include <utility>

namespace chronon3d::cli {

#include "video_job_execute_contract.inc"
#include "video_job_execute_options.inc"
#include "video_job_execute_dispatch.inc"

} // namespace chronon3d::cli
