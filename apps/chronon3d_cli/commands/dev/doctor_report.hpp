#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// doctor_report.hpp — the single canonical `chronon doctor` engine.
//
// One `run_doctor(DoctorOptions)` produces a `DoctorReport`; the CLI command
// (`command_doctor`) is a thin adapter that only formats the report (human or
// `--json`) and maps `ready` to the process exit code.  There are no per-area
// "DoctorX" subsystems: every check is a `DoctorCheck` in one report.
//
// Checks reuse the existing canonical infrastructure instead of inventing new
// probes:
//   - BackendRegistry (graph::BackendRegistry) for backend capability
//     introspection,
//   - AssetResolver (assets::AssetResolver) for asset-root resolution,
//   - TelemetryManager static host helpers for version/git/OS/core identity,
//   - the C ABI (`chronon_abi_version`/`chronon_version_string`) for the
//     engine identity when the C API is built.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

namespace chronon3d::cli {

enum class DoctorStatus {
    Pass,
    Warn,
    Fail,
    Skip,
};

struct DoctorCheck {
    std::string id;
    DoctorStatus status{DoctorStatus::Skip};
    std::string message;
};

struct DoctorReport {
    std::vector<DoctorCheck> checks;
    /// True when no check is in the `Fail` state.  `Warn` and `Skip` are
    /// advisory and do not block readiness.
    bool ready{false};
};

struct DoctorOptions {
    bool json{false};
    std::string assets_root;
    bool deep{false};
};

/// Run the canonical environment diagnostic.  Throws nothing; every probe is
/// wrapped so a single broken check degrades to `Fail` with a message rather
/// than aborting the report.
DoctorReport run_doctor(const DoctorOptions& options);

/// Lower-case canonical name for machine-readable output ("pass", "warn",
/// "fail", "skip").
[[nodiscard]] const char* doctor_status_name(DoctorStatus status) noexcept;

/// Upper-case canonical name for human output ("PASS", "WARN", "FAIL",
/// "SKIP").
[[nodiscard]] const char* doctor_status_label(DoctorStatus status) noexcept;

} // namespace chronon3d::cli
