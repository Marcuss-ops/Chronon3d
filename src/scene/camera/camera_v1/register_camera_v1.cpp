// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_v1.cpp
//
// Single entry-point for camera V1 built-in registration.
// Registers constraints, motions, and transitions, then freezes all registries.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/scene/camera/camera_v1/register_camera_motion_presets.hpp>
#include <chronon3d/scene/camera/camera_v1/register_camera_rig_motions.hpp>

namespace chronon3d::camera_v1 {

// Forward declare the per-module registration helpers.
void register_default_camera_constraints();

void register_camera_v1_builtins() {
    // Register constraints (5 builtins: look_at, keep_horizon, damped_follow, distance, rotation_limit).
    register_default_camera_constraints();

    // Register transitions (5 builtins: Cut, SmoothBlend, Push, WhipPan, FocusHandoff).
    CameraTransitionRegistry::instance().register_defaults();

    // Register the camera_motion::* + camera_rig::* motion presets with default
    // config. Replaces TODO(P7); see register_camera_motion_presets.cpp +
    // register_camera_rig_motions.cpp for the data definitions.
    register_camera_motion_presets();
    register_camera_rig_motions();

    // Freeze all registries so they become read-only + concurrent-safe.
    auto& cr = CameraConstraintRegistry::instance();
    if (!cr.is_frozen()) cr.freeze();

    auto& tr = CameraTransitionRegistry::instance();
    if (!tr.is_frozen()) tr.freeze();

    // Motion registry is freeze-gated on ids().empty() so a fresh registration
    // survives; once any preset registers (above), this becomes an unconditional
    // freeze on subsequent calls.
    auto& mr = CameraMotionRegistry::instance();
    if (!mr.is_frozen() && !mr.ids().empty()) mr.freeze();
}

} // namespace chronon3d::camera_v1
