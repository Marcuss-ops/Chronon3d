// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_v1.cpp
//
// Single entry-point for camera V1 built-in registration.
// Registers constraints, motions, and transitions, then freezes all registries.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>

namespace chronon3d::camera_v1 {

// Forward declare the per-module registration helpers.
void register_default_camera_constraints();

void register_camera_v1_builtins() {
    // Register constraints (5 builtins: look_at, keep_horizon, damped_follow, distance, rotation_limit).
    register_default_camera_constraints();

    // Register transitions (5 builtins: Cut, SmoothBlend, Push, WhipPan, FocusHandoff).
    CameraTransitionRegistry::instance().register_defaults();

    // Note: camera_motion::* and camera_rig::* legacy functions do the math
    // directly (src/scene/camera/camera_motion_presets.cpp, camera_rig.hpp).
    // The compiled CameraDescriptor path via builtin_camera_presets() +
    // compile_camera() in camera_program_compiler.cpp is the recommended path
    // for new code.

    // Freeze registries so they become read-only + concurrent-safe.
    auto& cr = CameraConstraintRegistry::instance();
    if (!cr.is_frozen()) cr.freeze();

    auto& tr = CameraTransitionRegistry::instance();
    if (!tr.is_frozen()) tr.freeze();
}

} // namespace chronon3d::camera_v1
