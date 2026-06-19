// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_v1.cpp
//
// Single entry-point for camera V1 built-in registration.
// Registers constraints, motions, and transitions, then freezes all registries.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

namespace chronon3d::camera_v1 {

static CameraTransitionCatalog s_transition_catalog;

const CameraTransitionCatalog& get_transition_catalog() {
    return s_transition_catalog;
}

void register_camera_v1_builtins() {
    // Register transitions (5 builtins: Cut, SmoothBlend, Push, WhipPan, FocusHandoff).
    s_transition_catalog.register_defaults();

    if (!s_transition_catalog.is_frozen()) s_transition_catalog.freeze();
}

} // namespace chronon3d::camera_v1
