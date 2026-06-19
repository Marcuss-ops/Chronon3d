#pragma once

// ProjectionContext struct and methods live in projection_context.hpp (no camera deps).
// This file adds the factory functions that require Camera / Camera2_5D.

#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <glm/gtc/constants.hpp>

namespace chronon3d::renderer {

inline ProjectionContext make_projection_context(const Camera& camera, i32 width, i32 height) {
    ProjectionContext out;
    out.view = camera.view_matrix();
    const f32 fw = static_cast<f32>(width);
    out.focal = camera.focal_length(fw);
    out.vp_cx = fw * 0.5f;
    out.vp_cy = static_cast<f32>(height) * 0.5f;
    return out;
}

inline ProjectionContext make_projection_context(const CameraProjectionSource& camera, i32 width, i32 height) {
    ProjectionContext out;
    out.view = camera.get_view_matrix();
    const f32 fw = static_cast<f32>(width);
    const f32 fh = static_cast<f32>(height);
    // Use the unified focal_from_camera which handles:
    //   - Physical lens + gate-fit (when use_physical_model is true)
    //   - Fov mode
    //   - Zoom mode (legacy)
    out.focal = chronon3d::camera_math::focal_from_camera(camera, fw, fh);
    out.vp_cx = fw * 0.5f;
    out.vp_cy = fh * 0.5f;
    return out;
}

template <typename Runtime>
inline void prepare_projection_context(Runtime& rt, const Camera& camera, i32 width, i32 height) {
    rt.projection = make_projection_context(camera, width, height);
    rt.projection.ready = true;
}

template <typename Runtime>
inline void prepare_projection_context(Runtime& rt, const CameraProjectionSource& camera, i32 width, i32 height) {
    rt.projection = make_projection_context(camera, width, height);
    rt.projection.ready = true;
}

} // namespace chronon3d::renderer
