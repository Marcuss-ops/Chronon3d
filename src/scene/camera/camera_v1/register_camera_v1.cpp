// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_v1.cpp
//
// Single entry-point for camera V1 built-in registration.
// Registers constraints, motions, and transitions, then freezes all registries.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

namespace chronon3d::camera_v1 {

void register_camera_v1_builtins() {
    // Register transitions (5 builtins: Cut, SmoothBlend, Push, WhipPan, FocusHandoff).
    CameraTransitionRegistry::instance().register_defaults();

    auto& tr = CameraTransitionRegistry::instance();
    if (!tr.is_frozen()) tr.freeze();
}

} // namespace chronon3d::camera_v1
