// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program_constraints.hpp
//
// FASE 4 Step 2 — Constraint evaluation extracted from camera_program.cpp.
//
// Contains:
//   - CompiledConstraintResult — result from applying a single constraint
//   - apply_constraint_spec()   — evaluate one CameraConstraintSpec
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>

#include <cstddef>
#include <string>

namespace chronon3d::camera_v1 {

/// Result from applying a single CameraConstraintSpec.
struct CompiledConstraintResult {
    Camera2_5D camera;
    bool       ok{true};
    std::string reason;
};

/// Apply a single CameraConstraintSpec to a camera.
/// @param session  Mutable session for stateful constraints (DampedFollow).
/// @param constraint_idx  Index of the constraint in the descriptor (for state slot).
CompiledConstraintResult apply_constraint_spec(
    const CameraConstraintSpec& spec,
    const Camera2_5D& in,
    const CameraEvalContext& ctx,
    CameraSession& session,
    std::size_t constraint_idx);

} // namespace chronon3d::camera_v1
