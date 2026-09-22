#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// doctor_report_internal.hpp — the private seam of the `chronon doctor` engine.
//
// TICKET-HOTSPOT-DECOMPOSITION A3: `doctor_report.cpp` used to be a shell that
// textually included three `*.inc` files (helpers + checks + public API). The
// engine is now its own translation unit and this header carries exactly the
// one cross-TU dependency: the canonical status-name switches.
//
//   doctor_report_internal.cpp   helpers + checks + run_doctor (the engine)
//   doctor_report.cpp            the public status-name façade
//
// Deliberately minimal: everything else in the engine (check collection,
// constants, probes, LogSilencer) stays internal to its TU, so the engine's
// private surface is one declaration pair rather than a new public API.
// ─────────────────────────────────────────────────────────────────────────────

#include "doctor_report.hpp"

namespace chronon3d::cli::doctor_detail {

/// The single status-name authority. `doctor_status_name` /
/// `doctor_status_label` in doctor_report.cpp are thin public façades over
/// these, so the machine-readable and human-readable vocabularies cannot drift
/// apart.
[[nodiscard]] const char* doctor_status_name_impl(DoctorStatus status) noexcept;
[[nodiscard]] const char* doctor_status_label_impl(DoctorStatus status) noexcept;

} // namespace chronon3d::cli::doctor_detail
