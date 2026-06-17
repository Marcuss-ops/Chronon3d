#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <vector>
#include <string>

namespace chronon3d {

struct CameraPathSample {
    int frame{0};
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    float target_center_error_px{0.0f};
    bool point_of_interest_enabled{false};  // stored during first evaluation (no re-eval)
};

struct CameraPathReport {
    std::vector<CameraPathSample> samples;

    float max_target_center_error_px{0.0f};
    float max_velocity_jump{0.0f};
    float max_acceleration_jump{0.0f};
    float max_jerk{0.0f};                   // max |Δacceleration| / dt

    bool passed{true};
    std::vector<std::string> failures;
};

// Forward declaration so callers can include only this header.
struct CameraPathValidationOptions;

/// Validates a camera path with configurable thresholds (Camera V1 — P1).
CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const TransformResolverResult& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    const CameraPathValidationOptions& opts,
    int step = 1
);

/// Legacy ABI-preserving overload (default opts = hardcoded 5.0f / 15.0f).
CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const TransformResolverResult& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    int step
);

/// SampleRange for the new analyze_camera_path API.
struct SampleRange {
    int start_frame{0};
    int end_frame{30};
    int step{1};
};

/// Analyzer entry-point (Camera V1 — P5 hardening). Replaces the old
/// `sample_camera_path()` as the canonical API. Returns a structured report
/// with velocity/acceleration/jerk stats and configurable validation.
CameraPathReport analyze_camera_path(
    const CameraRig& rig,
    const TransformResolverResult& transforms,
    Viewport viewport,
    SampleRange range,
    CameraPathValidationOptions options);

} // namespace chronon3d
