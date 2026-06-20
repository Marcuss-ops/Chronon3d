#pragma once

// ---------------------------------------------------------------------------
// runtime/job_telemetry.hpp
//
// Job-scoped telemetry aggregate.
//
// This header is created as part of the RenderSession extraction (see
// "Spostare e separare RenderSession" in the architecture plan).  In the
// end-state `JobTelemetry` will aggregate per-render-job counters:
// elapsed wall-clock time, frame count, accumulated dirty ratios, etc.
//
// At the moment of introduction this struct is intentionally a placeholder:
//   - fields will be added in a follow-up step
//   - `RenderSession::telemetry` already references this type so that
//     downstream code can begin referencing `RenderSession.telemetry.foo`
//     without source churn.
// ---------------------------------------------------------------------------

// #pragma message is supported by MSVC, clang, and gcc without conditionals.
// Emit it unconditionally so the SCAFFOLD warning fires on every compiler.
#pragma message("chronon3d/runtime/job_telemetry.hpp: SCAFFOLD - JobTelemetry placeholder body will be filled in a follow-up step. ABI surface pre-committed by design; do not add fields without updating the migration plan.")

namespace chronon3d {

/// Per-job telemetry (placeholder).
///
/// Intended to aggregate run-time metrics for a single render job.
/// Concrete fields will be introduced in a later step once the exact
/// telemetry shape is decided; until then, instances are default-constructed
/// and serve only as a stable name in the RenderSession member list.
struct JobTelemetry {
    // Reserved for future fields.  See RenderSession.telemetry.
};

} // namespace chronon3d
