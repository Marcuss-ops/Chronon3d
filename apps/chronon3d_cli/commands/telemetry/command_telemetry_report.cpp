#include "command_telemetry_internal.hpp"
#include "telemetry_report_model.hpp"
#include "telemetry_report_analyzer.hpp"
#include "telemetry_report_markdown.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>
#include <sstream>
#include <string>

namespace chronon3d::cli {

void generate_telemetry_report(std::stringstream& out, sqlite3* db, const std::string& run_id, const RunSummary& run) {
    ReportModel model = load_report_model(db, run_id, run);
    AnalysisResult analysis = analyze_report_model(model);
    generate_markdown_report(out, model, analysis);
}

} // namespace chronon3d::cli
#endif
