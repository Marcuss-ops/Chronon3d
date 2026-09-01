#include "doctor_report.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_graph/backend_registry.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#ifdef CHRONON3D_HAS_C_API
#include <chronon3d/c_api/chronon3d.h>
#endif

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace chronon3d::cli {

#include "doctor_report_helpers.inc"
#include "doctor_report_checks.inc"
#include "doctor_report_api.inc"

} // namespace chronon3d::cli
