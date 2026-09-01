#include "video_sink_adapter.hpp"

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

#include "video_sink_adapter_config.inc"
#include "video_sink_adapter_lifecycle.inc"
#include "video_sink_adapter_submit.inc"

} // namespace chronon3d::cli
