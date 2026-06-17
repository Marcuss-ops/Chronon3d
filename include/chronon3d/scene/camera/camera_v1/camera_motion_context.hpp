#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_motion_context.hpp
//
// The evaluation context for any CameraMotion, Constraint, or Trajectory
// sampler. All inputs are immutable; the plugin owns no hidden state.
//
// `t_seconds` is the sub-frame normalized time in seconds (NOT frames).
// `frame` is the canonical integer frame index (handy for beat lookups).
// ==============================================================================
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/frame_context.hpp>  // Frame

#include <chronon3d/math/glm_types.hpp>  // Vec2/Vec3 if present in engine

namespace chronon3d::camera_v1 {

/// Immutable per-sample evaluation context shared by Motion / Constraint / Trajectory.
struct CameraMotionContext {
    Frame         frame{0};                  ///< Integer frame index.
    SampleTime    sample_time;               ///< Continuous frame position (sub-frame aware).
    Vec3          base_position{0,0,0};      ///< Pre-motion camera position.
    Vec3          base_target{0,0,0};        ///< Pre-motion look-at target.
    f32           base_zoom{1.0f};           ///< Pre-motion zoom factor.
    f32           base_fov_deg{45.0f};       ///< Pre-motion field of view (degrees).
    f32           base_focus_distance{1000.0f}; ///< Pre-motion focus distance.
    f32           scene_unit_scale{1.0f};    ///< Unit scale of the scene (for unit-conversion).

    /// Factory: a deterministic context at integer frame `f`, no sub-frame.
    static CameraMotionContext at(Frame f) {
        CameraMotionContext c;
        c.frame = f;
        c.sample_time = SampleTime::from_frame_int(f);
        return c;
    }
};

} // namespace chronon3d::camera_v1
