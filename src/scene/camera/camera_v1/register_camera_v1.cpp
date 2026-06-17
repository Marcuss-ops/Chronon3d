// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_v1.cpp
//
// Single entry-point for camera V1 built-in registration.
// Checks each ID individually — no `ids().size()` shortcut.
// Freezes both registries after registration.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>

namespace chronon3d::camera_v1 {

// Forward declare the per-module registration helpers (defined in their
// respective registry .cpp files).
void register_default_camera_constraints();

void register_camera_v1_builtins() {
    // Register constraints — each ID checked individually.
    register_default_camera_constraints();

    // Freeze both registries so they become read-only + concurrent-safe.
    auto& cr = CameraConstraintRegistry::instance();
    if (!cr.is_frozen()) cr.freeze();

    auto& mr = CameraMotionRegistry::instance();
    if (!mr.is_frozen()) mr.freeze();
}

} // namespace chronon3d::camera_v1
