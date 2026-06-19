#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/register_camera_v1.hpp
//
// Single entry-point for camera V1 built-in registration.
//
// Call once at engine startup, BEFORE any CameraProgram evaluation:
//   register_camera_v1_builtins();
//
// After this call, the CameraTransitionCatalog is frozen — reads are
// concurrent-safe, writes are rejected.
//
// Idempotent: calling multiple times is safe (subsequent calls are no-ops
// because each ID is checked individually before registration).
// ==============================================================================

namespace chronon3d::camera_v1 {

class CameraTransitionCatalog;

/// Get the static transition catalog (populated by register_camera_v1_builtins).
const CameraTransitionCatalog& get_transition_catalog();

/// Register all built-in camera constraints and freeze both registries.
/// Idempotent — safe to call multiple times.
void register_camera_v1_builtins();

} // namespace chronon3d::camera_v1
