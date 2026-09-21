#pragma once

#include "../common/pipe_export_pipeline.hpp"

namespace chronon3d::cli {

/// Configure the scheduler owned by the native export path when the caller
/// did not provide an already-admitted scheduler.
[[nodiscard]] bool configure_local_device_scheduler(
    const FfmpegExportOptions& opts,
    runtime::DeviceScheduler& local_scheduler,
    runtime::DeviceScheduler*& active_scheduler);

} // namespace chronon3d::cli
