#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_path_validation.hpp
//
// CameraPathValidationOptions — configurable thresholds for the V1
// path-sampling validator. Default-constructed opts reproduce the existing
// hardcoded behavior (5.0f / 15.0f). Tests / power users can override.
//
// Part of Camera System V1 — P1 stabilization. The previous hardcoded
// thresholds lived in src/scene/camera/camera_path_sampler.cpp at lines
// 73 and 77 (max_target_center_error_px > 5.0f; max_acceleration_jump > 15.0f).
// The new opts struct lets callers raise/lower them per-shot without
// recompiling the engine.
//
// New fields covered here (no longer hardcoded):
//   - max_velocity_jump   — replaces the previously implicit limit
//   - max_jerk            — NEW for V1, was not present in the original sampler
//   - require_point_of_interest — flagship check for "camera has a target"
//
// Backward compatibility: every field has a sensible default that matches
// the prior hardcoded behavior. Callers that don't construct opts explicitly
// (or pass `CameraPathValidationOptions{}`) see the same numbers as before.
// ==============================================================================
#include <chronon3d/core/types/algebra_types.hpp>  // f32

namespace chronon3d {

/// Configurable knobs for the path-sampling validator. Override the defaults
/// per-shot to tighten or loosen the conformity bar.
struct CameraPathValidationOptions {
    f32  max_target_center_error_px{5.0f};   ///< target off-center, pixels
    f32  max_velocity_jump{50.0f};           ///< |Δvelocity| in scene-units /frame
    f32  max_acceleration_jump{15.0f};       ///< |Δaccel|   in scene-units /frame^2
    f32  max_jerk{30.0f};                    ///< |Δaccel| to |accel|, scene-units/frame^3
    bool require_point_of_interest{false};   ///< fail if camera.point_of_interest_enabled == false
};

} // namespace chronon3d
