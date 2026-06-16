#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <glm/gtx/quaternion.hpp>

namespace chronon3d {

// Forward-declare; defined below (after CameraPose needs it).
[[nodiscard]] Quat resolve_look_at_orientation(
    Vec3 camera_position, Vec3 target_position,
    Vec3 up_hint, f32 roll_deg) noexcept;

// ── CameraPose — position + orientation as a first-class type ─────────────
//
// Replaces the ad-hoc combination of Vec3 position + Vec3 rotation (Euler)
// that was previously scattered across Camera2_5D, CameraRig, and the
// animation system.  Orientation is stored as a Quat for gimbal-lock-free
// interpolation and robust view-matrix construction.
//
// Euler accessors are provided for backward compatibility but should be
// avoided in new code.  The Euler→Quat→Euler round-trip can introduce
// discontinuities near ±180°.
struct CameraPose {
    Vec3 position{0.0f, 0.0f, -1000.0f};
    Quat orientation{1.0f, 0.0f, 0.0f, 0.0f};  // identity

    // ── Derived directions in world space ─────────────────────────────────

    [[nodiscard]] Vec3 forward() const noexcept {
        return orientation * Vec3{0.0f, 0.0f, -1.0f};
    }

    [[nodiscard]] Vec3 right() const noexcept {
        return orientation * Vec3{1.0f, 0.0f, 0.0f};
    }

    [[nodiscard]] Vec3 up() const noexcept {
        return orientation * Vec3{0.0f, 1.0f, 0.0f};
    }

    // ── View matrix (look-at from pose) ───────────────────────────────────

    [[nodiscard]] Mat4 view_matrix() const noexcept {
        const Mat4 rot_mat = glm::toMat4(orientation);
        return glm::inverse(glm::translate(Mat4{1.0f}, position) * rot_mat);
    }

    /// Two-node variant: delegates to resolve_look_at_orientation()
    /// for robust up-vector handling, then applies roll.
    [[nodiscard]] Mat4 view_matrix(const Vec3& point_of_interest, f32 roll_deg = 0.0f) const noexcept {
        if (glm::length(point_of_interest - position) <= 0.001f)
            return view_matrix();

        const Quat look_orient = resolve_look_at_orientation(position, point_of_interest,
                                                              Vec3{0.0f, 1.0f, 0.0f}, roll_deg);
        const Mat4 rot_mat = glm::toMat4(look_orient);
        return glm::inverse(glm::translate(Mat4{1.0f}, position) * rot_mat);
    }

    // ── Legacy Euler accessors (backward compat) ──────────────────────────

    [[nodiscard]] Vec3 rotation_euler() const noexcept {
        return glm::degrees(glm::eulerAngles(orientation));
    }

    void set_rotation_euler(Vec3 euler_deg) noexcept {
        orientation = glm::quat(glm::radians(euler_deg));
    }

    // Equality (C++20 default)
    bool operator==(const CameraPose&) const = default;
};

// ── CameraUpMode — how the up-vector is resolved for look-at ──────────────

enum class CameraUpMode {
    WorldUp,          // fixed Vec3{0,1,0} (default)
    ExplicitVector,   // user-provided animated Vec3
    TargetLayer,      // world position of a named layer used as up-reference
    PathTransport     // parallel-transport from a precomputed LUT
};

// ── Resolve a look-at orientation from position, target, and up hint. ────
//
// Steps:
//   1. Normalise target - position → forward.
//   2. Project up_hint onto the plane orthogonal to forward.
//   3. If the projected up is near-zero, use a deterministic fallback axis.
//   4. Build an orthonormal basis (right, up, -forward) → quaternion.
//   5. Apply roll around the local forward axis.
//   6. Normalise the result.
//
// The result is deterministic for the same inputs — no frame-to-frame state.

[[nodiscard]] inline Quat resolve_look_at_orientation(
    Vec3 camera_position,
    Vec3 target_position,
    Vec3 up_hint,
    f32 roll_deg
) noexcept {
    const Vec3 forward = glm::normalize(target_position - camera_position);

    // Project up_hint onto the plane orthogonal to forward.
    Vec3 side = glm::normalize(glm::cross(up_hint, forward));
    Vec3 up   = glm::cross(forward, side);

    // Handle degenerate case: side vector near zero (up_hint ∥ forward).
    constexpr f32 kEpsilon = 1e-6f;
    if (glm::length2(side) < kEpsilon) {
        // Pick a fallback axis that is NOT parallel to forward.
        // When up_hint is the Y-axis and forward ≈ Y, switching to X
        // or Z guarantees a valid cross-product.
        const Vec3 fallback = (std::abs(forward.x) < 0.9f)
            ? Vec3{1.0f, 0.0f, 0.0f}
            : Vec3{0.0f, 0.0f, 1.0f};
        side = glm::normalize(glm::cross(fallback, forward));
        up   = glm::cross(forward, side);
    }

    // Build quaternion from the orthonormal basis.
    // The basis maps: local X→side, local Y→up, local Z→-forward.
    const glm::mat3 rot_mat(side, up, -forward);
    Quat orientation = glm::normalize(glm::quat_cast(rot_mat));

    // Apply roll around the local forward axis.
    if (std::abs(roll_deg) > 0.0001f) {
        const Quat roll = glm::angleAxis(glm::radians(roll_deg), Vec3{0.0f, 0.0f, -1.0f});
        orientation = glm::normalize(orientation * roll);
    }

    return orientation;
}

} // namespace chronon3d

// ── Backward-compatible free functions in chronon3d::math ──────────────────

namespace chronon3d::math {

inline Quat camera_rotation_quat(const Vec3& euler_degrees) {
    return glm::quat(glm::radians(euler_degrees));
}

inline Vec3 camera_rotation_euler(const Quat& rotation) {
    const Vec3 radians = glm::eulerAngles(rotation);
    return glm::degrees(radians);
}

inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation) {
    return glm::inverse(glm::translate(Mat4(1.0f), position) * glm::toMat4(rotation));
}

inline Mat4 camera_view_matrix(const Vec3& position, const Vec3& euler_degrees) {
    return camera_view_matrix(position, camera_rotation_quat(euler_degrees));
}

[[deprecated("Use CameraPose::view_matrix(point_of_interest, roll_deg) or resolve_look_at_orientation()")]]
inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation,
                               const Vec3& point_of_interest) {
    if (glm::length(point_of_interest - position) > 0.001f) {
        return glm::lookAt(position, point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
    }
    return camera_view_matrix(position, rotation);
}

} // namespace chronon3d::math
