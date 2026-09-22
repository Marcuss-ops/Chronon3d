// ═══════════════════════════════════════════════════════════════════════════
// doctor_report_internal.cpp — the `chronon doctor` engine.
//
// TICKET-HOTSPOT-DECOMPOSITION A3: this TU owns what used to be textually
// included into doctor_report.cpp as three `*.inc` files
// (doctor_report_helpers.inc + doctor_report_checks.inc + the engine half of
// doctor_report_api.inc).
//
//   doctor_report_helpers.inc  opens an anonymous namespace: constants, the
//                              check accumulator and the host probes. It is
//                              closed by the last line of checks.inc, so the
//                              two includes are one internal region and must
//                              stay adjacent and in this order.
//   doctor_report_checks.inc   the seven collect_* passes over that region.
//
// The public surface is `run_doctor` (declared in doctor_report.hpp) plus the
// two status-name switches shared with doctor_report.cpp through
// doctor_report_internal.hpp. Everything else stays internal to this TU.
// ═══════════════════════════════════════════════════════════════════════════

#include "doctor_report.hpp"
#include "doctor_report_internal.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_graph/backend_registry.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#ifdef CHRONON3D_HAS_C_API
#include <chronon/abi.h>
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

// ── the one dependency shared with doctor_report.cpp ───────────────────────
namespace doctor_detail {

const char* doctor_status_name_impl(DoctorStatus status) noexcept {
    switch (status) {
        case DoctorStatus::Pass: return "pass";
        case DoctorStatus::Warn: return "warn";
        case DoctorStatus::Fail: return "fail";
        case DoctorStatus::Skip: return "skip";
    }
    return "skip";
}

const char* doctor_status_label_impl(DoctorStatus status) noexcept {
    switch (status) {
        case DoctorStatus::Pass: return "PASS";
        case DoctorStatus::Warn: return "WARN";
        case DoctorStatus::Fail: return "FAIL";
        case DoctorStatus::Skip: return "SKIP";
    }
    return "SKIP";
}

} // namespace doctor_detail

// ── the internal region: helpers.inc opens the anonymous namespace, checks.inc
//    closes it. Kept adjacent on purpose.
#include "doctor_report_helpers.inc"
#include "doctor_report_checks.inc"

DoctorReport run_doctor(const DoctorOptions& options) {
    DoctorReport report;
    collect_engine_checks(report);
    collect_system_checks(report);
    collect_backend_checks(report);
    collect_encoder_checks(report);
    collect_asset_checks(report, options);
    collect_filesystem_checks(report);
    collect_deep_check(report, options);

    report.ready = true;
    for (const auto& check : report.checks) {
        if (check.status == DoctorStatus::Fail) {
            report.ready = false;
            break;
        }
    }
    return report;
}

} // namespace chronon3d::cli
