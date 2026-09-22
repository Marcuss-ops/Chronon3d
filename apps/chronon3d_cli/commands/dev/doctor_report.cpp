// ═══════════════════════════════════════════════════════════════════════════
// doctor_report.cpp — the public façade of the `chronon doctor` report.
//
// TICKET-HOTSPOT-DECOMPOSITION A3: the engine now lives in its own translation
// unit (doctor_report_internal.cpp) instead of being textually included here.
// What remains is the pair of public status-name functions declared in
// doctor_report.hpp, delegating to the engine's single switch authority so the
// machine-readable ("pass") and human-readable ("PASS") vocabularies cannot
// drift apart.
// ═══════════════════════════════════════════════════════════════════════════

#include "doctor_report.hpp"
#include "doctor_report_internal.hpp"

namespace chronon3d::cli {

const char* doctor_status_name(DoctorStatus status) noexcept {
    return doctor_detail::doctor_status_name_impl(status);
}

const char* doctor_status_label(DoctorStatus status) noexcept {
    return doctor_detail::doctor_status_label_impl(status);
}

} // namespace chronon3d::cli
