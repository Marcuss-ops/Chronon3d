#pragma once

// ============================================================================
// camera_projection_source.hpp — Projection Source (non-owning view)
//
/// @file    camera_projection_source.hpp
/// @brief   Lightweight non-owning wrapper around any camera that provides the
///          projection pipeline with read-only access to camera properties.
///
/// CameraProjectionSource is NOT a base class — it's a thin view that holds
/// a const pointer to the concrete camera.  This preserves aggregate
/// initialization on Camera2_5D and avoids vtable overhead.
///
/// An implicit conversion from Camera2_5D allows the projection pipeline to
/// accept a CameraProjectionSource in place of a concrete camera.
///
/// All getter implementations are inline and defined here so that every
/// translation unit that uses CameraProjectionSource can link.
// ============================================================================

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>

namespace chronon3d {

// ── Projection enums (canonical location) ────────────────────────────────────

enum class Camera2_5DProjectionMode { Zoom, Fov };
enum class CameraOpticsMode { Zoom, FieldOfView, PhysicalLens };

// Forward declaration — the full type lives in camera_2_5d.hpp.
struct Camera2_5D;

// ── CameraProjectionSource — non-owning projection view ──────────────────────
class CameraProjectionSource {
public:
    CameraProjectionSource() = default;

    /// Implicit conversion from Camera2_5D.
    /// Defined in camera_2_5d.hpp where the full type is visible.
    /* implicit */ CameraProjectionSource(const Camera2_5D& cam);

    // ── Inline getters ───────────────────────────────────────────────────
    // Access the concrete camera through m_cam.  The accessor functions
    // are defined in camera_2_5d.hpp where Camera2_5D's layout is known,
    // but a forward-declared pointer is sufficient for storage.

    [[nodiscard]] Vec3 get_position() const;
    [[nodiscard]] f32   get_zoom() const;
    [[nodiscard]] f32   get_fov_deg() const;
    [[nodiscard]] Camera2_5DProjectionMode get_projection_mode() const;
    [[nodiscard]] CameraOpticsMode get_optics_mode() const;
    [[nodiscard]] const LensModel& get_lens() const;
    [[nodiscard]] bool  get_dof_use_physical_model() const;
    [[nodiscard]] Mat4  get_view_matrix() const;

    [[nodiscard]] bool valid() const { return m_cam != nullptr; }

private:
    friend struct Camera2_5D;
    const Camera2_5D* m_cam{nullptr};
};

} // namespace chronon3d
