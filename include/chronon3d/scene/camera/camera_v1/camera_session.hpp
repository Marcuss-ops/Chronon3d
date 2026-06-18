#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_session.hpp
//
// CameraSession — mutable per-evaluation session, separated from constraints.
//
// The session owns the ConstraintSession (pre-allocated per-constraint state
// slots) and the banking roll accumulator.  The compiler pre-allocates exactly
// the number of constraint slots needed so there is no allocation in the hot
// path.
//
// Usage (compiled path):
//   CameraSession session;
//   session.ensure_constraint_states(program.num_constraints());
//   auto result = program.evaluate(ctx, session);
//
// Usage (builder path — unchanged):
//   ConstraintSession session;
//   session.ensure_states(prog.constraints_.size());
//   auto result = prog.evaluate(ctx, session);
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>  // ConstraintSession, ConstraintState

namespace chronon3d::camera_v1 {

/// Top-level session for the compiled camera evaluation path.
/// Wraps constraint state + banking roll + any future per-program state.
struct CameraSession {
    /// Constraint state slots (one per constraint, index-aligned).
    ConstraintSession constraint_session;

    /// Banking roll accumulator (shared state across frames).
    float banking_roll{0.0f};

    /// Ensure at least n constraint state slots are allocated.
    void ensure_constraint_states(std::size_t n) {
        constraint_session.ensure_states(n);
    }

    /// Reset all session state (call at the start of a new render).
    void reset() {
        constraint_session.reset();
        banking_roll = 0.0f;
    }
};

} // namespace chronon3d::camera_v1
