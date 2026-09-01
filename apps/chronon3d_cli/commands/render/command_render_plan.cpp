#include "../../command_registry.hpp"
#include "../../cli_context.hpp"
#include "../../utils/job/render_job.hpp"
#include "command_render_plan.hpp"
#include "render_plan_preparation.hpp"

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/verification/render_receipt.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace chronon3d::cli {

#include "command_render_plan_internal.inc"
#include "command_render_plan_api.inc"

} // namespace chronon3d::cli
