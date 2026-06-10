#pragma once

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include "telemetry_report_model.hpp"
#include "telemetry_report_analyzer.hpp"
#include <ostream>

namespace chronon3d::cli {

void generate_markdown_report(std::ostream& out, const ReportModel& model, const AnalysisResult& analysis);

} // namespace chronon3d::cli
#endif
