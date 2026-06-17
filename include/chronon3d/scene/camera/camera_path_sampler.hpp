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
};

struct CameraPathReport {
    std::vector<CameraPathSample> samples;

    float max_target_center_error_px{0.0f};
    float max_velocity_jump{0.0f};
    float max_acceleration_jump{0.0f};

    bool passed{true};
    std::vector<std::string> failures;
};

CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const TransformResolverResult& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    int step
);

// Forward declaration so callers can include only this header.
// The full definition lives in camera_path_validation.hpp (P1).
struct CameraPathValidationOptions;

/// Configurable-thresholds overload (Camera V1 — P1). Default opts preserve
/// the previously hardcoded behavior (5.0f target error, 15.0f accel jump).
CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const TransformResolverResult& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    const CameraPathValidationOptions& opts,
    int step = 1
);

} // namespace chronon3d
