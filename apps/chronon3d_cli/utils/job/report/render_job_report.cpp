#include "render_job_report.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/bottleneck_analyzer.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <functional>

namespace chronon3d::cli {

#include "render_job_report_diagnostics.inc"
#include "render_job_report_output.inc"

} // namespace chronon3d::cli
