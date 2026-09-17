#include "../../command_registry.hpp"
#include "../../cli_context.hpp"
#include "../../utils/job/render_job.hpp"
// Same pixel-truth authority the direct `render` route uses, so the two routes
// cannot describe the selected output lane differently
// (TICKET-IMAGE-PATH-PIXEL-TRUTH).
#include "../../utils/job/output_lane_pixel_truth.hpp"
#include "command_render_plan.hpp"
#include "render_plan_preparation.hpp"
#include "../video/common/pipe_pixfmt_vocabulary.hpp"
// ONE sink vocabulary for the --video-sink option checker (sink_options.hpp).
#include "../video/common/sink_options.hpp"
#include "../../utils/video/encoder_config_resolution.hpp"
#include "../../utils/video/copy_lane_coverage.hpp"

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#ifdef CHRONON3D_ENABLE_VERIFICATION
#include <chronon3d/verification/render_receipt.hpp>
#endif
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace chronon3d::cli {

#include "command_render_plan_internal.inc"
#include "command_render_plan_api.inc"

} // namespace chronon3d::cli
