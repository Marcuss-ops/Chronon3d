#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/register_camera_v1.hpp
//
// CAM-04/05 (DOC 03) — Per-job ownership enforcement for camera V1.
//
// Historical context: this header previously declared:
//
//   - `get_transition_catalog()` — exposed a process-wide static singleton;
//   - `register_camera_v1_builtins()` — populated the static singleton.
//
// Both were removed in CAM-05 because DOC 03 §5 requires that camera
// state mutation (and the mutex acquisition that comes with it) be
// confined to per-job lifetimes, never process-global.
//
// The new entry point is `register_camera_v1_builtins_into(catalog)` below:
// callers own the catalog and pass it by reference.  Typical usage from
// `RenderRuntime::initialise()`:
//
//   m_owned_transition_catalog = std::make_unique<CameraTransitionCatalog>();
//   register_camera_v1_builtins_into(*m_owned_transition_catalog);
//
// Reference: docs/camera-plan/03-MOTION_TRAJECTORY_TIMELINE_DETERMINISM.md
//            §5 (Per-job ownership enforcement).
// ==============================================================================

namespace chronon3d::camera_v1 {

class CameraTransitionCatalog;

/// Register all built-in camera transitions (Cut / SmoothBlend / Push /
/// WhipPan / FocusHandoff) into the CALLER-OWNED `catalog` reference.
///
/// Lifetime: `catalog` must outlive all usages of the resulting
/// transitions — typically owned by the render runtime.
///
/// Thread-safety: idempotent.  After this call `catalog` is frozen for
/// concurrent-safe reads; if the catalog was already frozen by a prior
/// call, this is a no-op.
void register_camera_v1_builtins_into(CameraTransitionCatalog& catalog);

} // namespace chronon3d::camera_v1
