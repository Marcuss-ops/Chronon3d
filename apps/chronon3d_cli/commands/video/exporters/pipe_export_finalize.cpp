#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "../../../utils/telemetry/telemetry_run.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/media/video/output_contract.hpp>

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#endif

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>

#include "pipe_export_finalize_encoder.inc"
#include "pipe_export_finalize_telemetry.inc"
