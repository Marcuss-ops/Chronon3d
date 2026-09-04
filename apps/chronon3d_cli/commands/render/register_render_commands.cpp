#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../daemon/daemon_service.hpp"
#include "../../utils/common/cli_mappers.hpp"
#include "../../utils/common/props_file.hpp"
#include "../../utils/common/props_inline.hpp"
#include "../../utils/common/render_job_error_formatter.hpp"
#include "../../utils/job/render_job.hpp"
#include "render_profiles.hpp"
#include "command_render_plan.hpp"
#include "render_plan_preparation.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <limits>
#include <cstdint>

namespace chronon3d::cli {

#include "register_render_commands_support.inc"
#include "register_render_commands_body.inc"

} // namespace chronon3d::cli
