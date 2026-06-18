#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_constraint.hpp
//
// ConstraintSession — per-constraint state slots for stateful constraints
// (DampedFollow). Used by the compiled camera evaluation path.
//
// CameraConstraint class hierarchy and factory functions removed in PR12.
// The compiled path uses CameraConstraintSpec variants directly
// (defined in camera_descriptor.hpp).
// ==============================================================================
#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D
#include <chronon3d/core/types/sample_time.hpp>

#include <vector>

namespace chronon3d::camera_v1 {

// =========================================================================
// ConstraintSession — per-constraint slots for stateful constraints.
// =========================================================================

/// Per-constraint slot: state that survives across frames for one constraint.
struct ConstraintState {
    Camera2_5D previous_camera{};
    Vec3       previous_velocity{0,0,0};
    SampleTime previous_time{};
    bool       has_previous{false};
};

/// Session passed to constraint evaluation functions (compiled path).
/// Contains one slot per constraint (aligned to the stack index).
struct ConstraintSession {
    std::vector<ConstraintState> states;
    std::size_t                  active_index{0};
    float banking_roll{0.0f};   // shared banking state (CameraProgram)

    void ensure_states(std::size_t n) {
        if (states.size() < n) states.resize(n);
    }

    ConstraintState& active_state() { return states.at(active_index); }
    const ConstraintState& active_state() const { return states.at(active_index); }

    void reset() {
        for (auto& s : states) s = {};
        active_index = 0;
        banking_roll = 0.0f;
    }
};

} // namespace chronon3d::camera_v1
