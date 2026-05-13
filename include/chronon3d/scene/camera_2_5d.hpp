#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec3.hpp>

namespace chronon3d {

struct MotionBlurSettings {
    bool enabled{false};
    int  samples{8};           // number of subframes to accumulate
    f32  shutter_angle{180.0f}; // degrees; 180 = half-frame exposure
};

struct DepthOfFieldSettings {
    bool  enabled{false};
    f32   focus_z{0.0f};      // world-space Z at which blur = 0
    f32   aperture{0.015f};   // blur per unit of Z distance from focus
    f32   max_blur{24.0f};    // clamp: pixels of blur at extreme depths
};

// How perspective strength is specified for Camera2_5D.
// Zoom: use camera.zoom directly (focal_length = zoom).
// Fov:  derive focal length from camera.fov_deg and viewport height.
enum class Camera2_5DProjectionMode {
    Zoom, // default — focal length == camera.zoom
    Fov   // derive focal from fov_deg (true FOV-based projection)
};

struct Camera2_5D {
    bool enabled{false};

    // Camera position in screen-space world coordinates.
    // Default: 1000 units "in front" of the z=0 plane.
    Vec3 position{0.0f, 0.0f, -1000.0f};

    // Stored for future two-node camera support; not used for orbit in v1.
    Vec3 point_of_interest{0.0f, 0.0f, 0.0f};

    // Projection mode: Zoom (default) or Fov.
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    // Perspective strength. At depth == zoom, perspective_scale == 1.
    // Used when projection_mode == Zoom.
    f32 zoom{1000.0f};

    // Field of view in degrees. Used when projection_mode == Fov.
    // 35° ≈ telephoto (less distortion), 70° ≈ wide angle (more parallax).
    f32 fov_deg{50.0f};

    DepthOfFieldSettings dof;
};

} // namespace chronon3d
