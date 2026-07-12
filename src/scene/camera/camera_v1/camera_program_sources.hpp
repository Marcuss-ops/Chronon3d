// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program_sources.hpp
//
// FASE 4 Step 1 — Source evaluation helpers extracted from camera_program.cpp.
//
// Free functions for evaluating CameraSourceSpec variants:
//   - source_is_time_dependent()  — fast predicate for cached is_animated
//   - apply_projection_spec()     — central ProjectionSpec → Camera2_5D dispatch
//   - eval_pose_tracks()          — sample all animated PoseTracksSource channels
//   - eval_orbit_motion()         — sample OrbitMotion spherical params
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>

#include <type_traits>
#include <variant>

namespace chronon3d::camera_v1 {

// =============================================================================
// LookAheadResult — returned by look_ahead_tangent() for OrientAlongPath.
// =============================================================================
struct LookAheadResult {
    bool used{false};
    Vec3 tangent{0.0f, 0.0f, 0.0f};
};

/// Sample the trajectory at t+Δ and return the look-ahead tangent if valid.
LookAheadResult look_ahead_tangent(const CameraSourceSpec& source,
                                   const CameraEvalContext& ctx,
                                   float delta_seconds = 1.0f / 60.0f);

// ── Source time-dependency check ──────────────────────────────────────────

/// Check if a CameraSourceSpec variant is time-dependent.
bool source_is_time_dependent(const CameraSourceSpec& source);

// ── Projection spec dispatch (CAM-03 / DOC 02) ────────────────────────────

/// Apply the active ProjectionSpec (Zoom / Fov / PhysicalLens) to the camera.
void apply_projection_spec(const ProjectionSpec& spec,
                           const CameraEvalContext& ctx,
                           Camera2_5D& cam);

// ── Source evaluators ─────────────────────────────────────────────────────

/// Evaluate a PoseTracksSource by sampling keyframed position/rotation/target
/// + projection and DOF channels.
Camera2_5D eval_pose_tracks(const CameraBaseSpec& base,
                            const PoseTracksSource& src,
                            const CameraEvalContext& ctx);

/// Evaluate an OrbitMotion by sampling spherical params and translating
/// track + dolly in the camera-local basis (TICKET-024 fix).
Camera2_5D eval_orbit_motion(const CameraBaseSpec& base,
                             const OrbitMotion& orbit,
                             const CameraEvalContext& ctx,
                             bool is_animated);

} // namespace chronon3d::camera_v1
