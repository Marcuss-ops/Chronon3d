#pragma once

// ============================================================================
// include/chronon3d/timeline/render_job.hpp
//
// P3-C (V0.2 timeline staging) — `RenderJob` lives NEXT to the legacy
// `Composition` class.  It formalises the V2 per-frame orchestration payload:
//     RenderSession + CameraSession + RenderDiagnostics
//
// The new `RenderDiagnostics` struct is INTRODUCED in this commit (it's not
// a legacy alias).  It is deliberately a placeholder for the upcoming V2
// diagnostics surface; fields will be added over the next few V2 PRs as
// every legacy shared-state tracking variable in render_engine.cpp gets
// folded into a typed observer.
//
// Anti-DRY note (Rule 4 ANTI_DUPLICATION_RULES):
//   `RenderJob` is the V2 staging synonym for the per-frame coupling that
//   today lives implicitly across `RenderEngine::Impl` + `Runtime::attach_backend`
//   + the per-worker `CameraSessionCache`.  It is a struct, not a service;
//   there is no `RenderJobFactory`, `RenderJobBuilder`, or `RenderJobRegistry`.
// ============================================================================

#include <cstdint>                                                  // std::uint32_t

#include <chronon3d/internal/runtime/render_session.hpp>                     // RenderSession
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>     // camera_v1::CameraSession

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::RenderDiagnostics  (P3-C placeholder)
//
//   V2 staging struct.  Sits NEXT to (does NOT replace) any legacy diagnostics
//   surface (RenderDiagnostic in chronon3d::render:: is a singular internal-marker
//   type for render-graph nodes and is intentionally NOT surfaced here \u2014 V2
//   uses an ACCUMULATOR pattern, single-static-instance per RenderJob, which is
//   what this struct will eventually hold).
//
//   `version` \u2014 monotonically-incrementing schema tag for the V2 diagnostics
//             shape.  Bumped per V2 field-add so consumers can gate against
//             stale assumptions.
//
// Placeholder fields will be added in subsequent V2 PRs:
//     - frame_timings                  (per-stage stopwatch row)
//     - cache_stats                     (compile + per-hit/per-miss counters)
//     - camera_program_session_view     (per-worker checkpoint ids)
//     - composition_dispatch_trace      (event log of composition dispatch decisions)
// ─────────────────────────────────────────────────────────────────────────────
struct RenderDiagnostics {
    std::uint32_t version{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::RenderJob
//
//   V2 staging struct.  Sits NEXT to (does NOT replace) anything; it is the
//   per-frame orchestration payload that the future V2 render driver will
//   assemble once per frame, then route through `RenderEngine::Impl`.
//
//   * `render_session`  \u2014 the per-call `RenderSession` (lifetime bound to one
//                          frame); engines borrow services from the runtime,
//                          the session owns the per-call state.
//   * `camera_session`  \u2014 the per-call `camera_v1::CameraSession` (per-worker
//                          from `CameraSessionCache::acquire(...)`).
//   * `diagnostics`     \u2014 the per-call `RenderDiagnostics` accumulator (V2
//                          staging placeholder; see comment above).
//
//   Move-only contract:  `RenderSession` contains `unique_ptr` fields
//   (`FrameArena`, `SceneProgramStore`, …); embedding it by value makes
//   `RenderJob` itself MOVE-ONLY.  Copy is deleted to surface the move-only
//   intent at the API level rather than papering over it.
// ─────────────────────────────────────────────────────────────────────────────
struct RenderJob {
    RenderSession                       render_session{};
    camera_v1::CameraSession            camera_session{};
    RenderDiagnostics                   diagnostics{};

    RenderJob() = default;
    RenderJob(RenderJob&&) noexcept = default;
    RenderJob& operator=(RenderJob&&) noexcept = default;
    RenderJob(const RenderJob&) = delete;
    RenderJob& operator=(const RenderJob&) = delete;
};

} // namespace chronon3d
