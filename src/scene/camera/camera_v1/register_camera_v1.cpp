// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_v1.cpp
//
// CAM-04/05 (DOC 03) — Per-job ownership enforcement.
//
// THIS FILE IS A NO-OP STUB BY DESIGN after the CAM-05 migration.
//
// Historical context: this translation unit previously hosted three
// `static` / global-state functions:
//
//   - `static CameraTransitionCatalog s_transition_catalog;`
//   - `const CameraTransitionCatalog& get_transition_catalog();`
//   - `void register_camera_v1_builtins();`
//
// These violated the CAM-05 per-job ownership rule (DOC 03 §5) because
// they froze the catalog during static initialization, then exposed a
// process-wide singleton that any `ShotTimelineResolver` could reach via
// a global lookup.  The `CameraTransitionCatalog` itself owns a
// `std::mutex mu_` that was being acquired on every transition lookup —
// a hot-path global lock during render-graph evaluation.
//
// The migration (CAM-05 PR1):
//   1. The catalog is now owned BY THE RENDER JOB (typically held inside
//      `RenderRuntime::m_owned_transition_catalog` or passed as an
//      explicit constructor argument to `ShotTimelineResolver`).
//   2. The bootstrap is `register_camera_v1_builtins_into(catalog)` below:
//      callers pass their own catalog reference, no global state involved.
//   3. The library `ShotTimelineResolver::default_*()` factories are still
//      available for unit tests that don't bootstrap a full catalog.
//
// Reference: docs/camera-plan/03-MOTION_TRAJECTORY_TIMELINE_DETERMINISM.md
//            §5 (Per-job ownership enforcement).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

namespace chronon3d::camera_v1 {

/// CAM-05 / DOC 03 — Inject the built-in transitions into a CALLER-OWNED
/// catalog.  This is the per-job replacement for the legacy static-singleton
/// `register_camera_v1_builtins()` (removed in CAM-05).
///
/// The caller is responsible for the lifetime of `catalog` — typically
/// `RenderRuntime` owns it as `std::unique_ptr<CameraTransitionCatalog>` and
/// passes `&*m_owned_transition_catalog` here at engine bootstrap.  After
/// this call the catalog is frozen and reads are concurrent-safe.
///
/// Idempotent: calling multiple times is safe (each ID is registered only
/// once, and subsequent registrations of a frozen catalog throw on the
/// default policy).
void register_camera_v1_builtins_into(CameraTransitionCatalog& catalog) {
    catalog.register_defaults();
    if (!catalog.is_frozen()) catalog.freeze();
}

} // namespace chronon3d::camera_v1
