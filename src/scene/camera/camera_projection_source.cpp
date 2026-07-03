// ============================================================================
// camera_projection_source.cpp — CameraProjectionSource getter implementations
//
/// @file    camera_projection_source.cpp
/// @brief   Out-of-line definitions for CameraProjectionSource getters.
///          Separated from the header to avoid circular includes between
///          camera_projection_source.hpp and camera_2_5d.hpp.
// ============================================================================

#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

namespace chronon3d {

// ── Constructor ──────────────────────────────────────────────────────────────

CameraProjectionSource::CameraProjectionSource(const Camera2_5D& cam)
    : m_cam(&cam) {}

// ── Getters ─────────────────────────────────────────────────────────────────

Vec3 CameraProjectionSource::get_position() const {
    return m_cam->position;
}

f32 CameraProjectionSource::get_zoom() const {
    return m_cam->zoom;
}

f32 CameraProjectionSource::get_fov_deg() const {
    return m_cam->fov_deg;
}

CameraOpticsMode CameraProjectionSource::get_optics_mode() const {
    return m_cam->optics_mode;
}

const LensModel& CameraProjectionSource::get_lens() const {
    return m_cam->lens;
}

bool CameraProjectionSource::get_dof_use_physical_model() const {
    return m_cam->dof.use_physical_model;
}

Mat4 CameraProjectionSource::get_view_matrix() const {
    return m_cam->view_matrix();
}

} // namespace chronon3d
