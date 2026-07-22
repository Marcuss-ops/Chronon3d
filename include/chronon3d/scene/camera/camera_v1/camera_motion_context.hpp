#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_motion_context.hpp
//
// Evaluation contexts for camera evaluation.
//
// CameraMotionContext   — legacy context with duplicated base state (kept for
//                         backward compatibility with existing evaluators).
// CameraEvalContext     — slim context without base state duplication.
//                         The base state lives in the compiled CameraProgram.
//
// `t_seconds` is the sub-frame normalized time in seconds (NOT frames).
// `frame` is the canonical integer frame index (handy for beat lookups).
// ==============================================================================
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/frame_context.hpp>  // Frame

#include <chronon3d/math/glm_types.hpp>  // Vec2/Vec3 if present in engine

#include <cstdint>

namespace chronon3d {
// Forward declaration of ResolvedSceneTransforms (defined in hierarchy_resolver.hpp).
struct ResolvedSceneTransforms;
} // namespace chronon3d

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
    /// CAM-05: FrameRate is now explicit — no more hardcoded 30 fps default.
    static CameraMotionContext at(Frame f, FrameRate frame_rate) {
        CameraMotionContext c;
        c.frame = f;
        c.sample_time = SampleTime::from_frame_int(f, frame_rate);
        return c;
    }
};

// =============================================================================
// CameraEvalContext — slimmed-down evaluation context for the compiled path.
//
// Unlike CameraMotionContext, this struct does NOT carry base_* fields.
// The base state (position, target, zoom, FOV, focus distance) lives in the
// compiled CameraBaseSpec inside the CameraProgram, eliminating the
// duplication described in the camera architecture document.
//
// Fields:
//   frame         — integer frame index.
//   sample_time   — continuous sub-frame position.
//   viewport_*    — output dimensions (for gate-fit, FOV conversion).
//   transforms    — optional snapshot of resolved layer transforms
//                   (for LookAtLayer and hierarchy binding).
// =============================================================================

struct CameraEvalContext {
    Frame      frame{0};
    SampleTime sample_time;

    std::int32_t viewport_width{1920};
    std::int32_t viewport_height{1080};

    /// Optional transform snapshot for hierarchy / LookAtLayer resolution.
    /// nullptr means transforms are not available (LookAtLayer falls back
    /// to a diagnostic warning in the compiled path).
    const ResolvedSceneTransforms* transforms{nullptr};

    /// Return a copy with the frame and sample_time updated.
    [[nodiscard]] CameraEvalContext with_frame(Frame f, FrameRate frame_rate) const {
        CameraEvalContext c = *this;
        c.frame = f;
        c.sample_time = SampleTime::from_frame_int(f, frame_rate);
        return c;
    }

    /// Factory: a deterministic context at integer frame `f`, no sub-frame.
    /// CAM-05: FrameRate is now explicit — no more hardcoded 30 fps default.
    static CameraEvalContext at(Frame f, FrameRate frame_rate,
                                 std::int32_t vp_w = 1920,
                                 std::int32_t vp_h = 1080) {
        CameraEvalContext c;
        c.frame = f;
        c.sample_time = SampleTime::from_frame_int(f, frame_rate);
        c.viewport_width = vp_w;
        c.viewport_height = vp_h;
        return c;
    }
};

} // namespace chronon3d::camera_v1
