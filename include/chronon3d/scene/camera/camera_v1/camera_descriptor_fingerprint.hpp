#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp
//
// Deterministic fingerprint (FNV-1a) for CameraDescriptor.
// Split from camera_descriptor.hpp (STEP 8).
//
// compute_camera_descriptor_fingerprint(desc):
//   Two descriptors with identical content hash to the same value.
//   Heap-allocated identities (shared_ptr, vector capacities, padding)
//   are NOT hashed — only the *content* is considered.
//
// DETERMINISM: FNV-1a 64-bit, IEEE-754 float bits via memcpy, structural
// iteration order of fields.  Identical descriptors ⇒ identical hash.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <cstdint>
#include <cstring>

namespace chronon3d::camera_v1 {

namespace detail {

/// FNV-1a 64-bit streaming hasher.  simple, deterministic, bit-stable.
class Fnv1aHasher {
public:
    std::uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a 64 offset basis.
    static constexpr std::uint64_t kPrime = 0x100000001b3ULL;

    void mix_bytes(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= kPrime;
        }
    }

    void mix_u8 (std::uint8_t  v) { mix_bytes(&v, sizeof(v)); }
    void mix_u32(std::uint32_t v) { mix_bytes(&v, sizeof(v)); }
    void mix_u64(std::uint64_t v) { mix_bytes(&v, sizeof(v)); }
    void mix_f32(float         v) { mix_bytes(&v, sizeof(v)); }
    void mix_i32(std::int32_t   v) { mix_bytes(&v, sizeof(v)); }

    void mix_str(std::string_view s) {
        mix_u64(s.size());
        if (!s.empty()) mix_bytes(s.data(), s.size());
    }
    void mix_bool(bool b) { mix_u8(b ? 1u : 0u); }
    void mix_vec3(const Vec3& v) { mix_f32(v.x); mix_f32(v.y); mix_f32(v.z); }

    template<typename E>
    void mix_enum(E v) { mix_u32(static_cast<std::uint32_t>(v)); }
};

template <typename T>
inline void hash_animated_value(Fnv1aHasher& h,
                                const chronon3d::AnimatedValue<T>& av) {
    h.mix_bool(av.is_time_dependent());
    const auto& kfs = av.keyframes();
    h.mix_u64(kfs.size());
    for (const auto& kf : kfs) {
        h.mix_i32(kf.frame.value);
        h.mix_bytes(&kf.value, sizeof(T));
        h.mix_enum(kf.interp);
        h.mix_bool(kf.roving);
        h.mix_enum(kf.easing.preset);
        h.mix_bool(kf.easing.cubic.has_value());
        if (kf.easing.cubic.has_value()) {
            h.mix_f32(kf.easing.cubic->x1);
            h.mix_f32(kf.easing.cubic->y1);
            h.mix_f32(kf.easing.cubic->x2);
            h.mix_f32(kf.easing.cubic->y2);
        }
    }
}

} // namespace detail

// ============================================================================
// compute_camera_descriptor_fingerprint
//
// Deterministic, address-independent hash of a CameraDescriptor.
// Covers: id, source, orientation, constraints, lens, base, modifiers,
// failure_policy.  Excludes pointer identity (trajectory shared_ptr).
// ============================================================================

inline std::uint64_t compute_camera_descriptor_fingerprint(
    const CameraDescriptor& desc) {
    detail::Fnv1aHasher h;

    // ── Identifier + policy ────────────────────────────────────────
    h.mix_str(desc.id);
    h.mix_enum(desc.failure_policy);

    // ── base spec ─────────────────────────────────────────────────
    h.mix_bool(desc.base.enabled);
    h.mix_vec3(desc.base.position);
    h.mix_vec3(desc.base.rotation);
    h.mix_bool(desc.base.point_of_interest_enabled);
    h.mix_vec3(desc.base.point_of_interest);
    h.mix_str(desc.base.parent_name);

    // ── Lens / DOF / motion blur ──────────────────────────────────
    {
        const auto& lens = desc.base.lens;
        h.mix_f32(lens.focal_length);
        h.mix_f32(lens.f_stop);
        h.mix_f32(lens.close_focus);
        h.mix_f32(lens.sensor_width);
        h.mix_f32(lens.sensor_height);
        h.mix_enum(lens.gate_fit);
    }
    {
        const auto& dof = desc.base.dof;
        h.mix_bool(dof.enabled);
        h.mix_f32(dof.focus_z);
        h.mix_f32(dof.aperture);
        h.mix_f32(dof.max_blur);
        h.mix_f32(dof.focus_distance);
        h.mix_bool(dof.use_physical_model);
        h.mix_f32(dof.near_bokeh_radius);
        h.mix_f32(dof.far_bokeh_radius);
    }
    {
        const auto& mb = desc.base.motion_blur;
        h.mix_u8(static_cast<std::uint8_t>(mb.mode));
        h.mix_f32(mb.shutter_angle_deg);
    }

    // ── projection variant ────────────────────────────────────────
    h.mix_u8(desc.base.projection.index());
    std::visit([&h](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, ZoomProjection>) {
            detail::hash_animated_value(h, p.zoom);
        } else if constexpr (std::is_same_v<T, FovProjection>) {
            detail::hash_animated_value(h, p.fov_deg);
        }
    }, desc.base.projection);

    // ── source variant ────────────────────────────────────────────
    h.mix_u8(desc.source.index());
    std::visit([&h](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, StaticCameraSource>) {
            // No fields.
        } else if constexpr (std::is_same_v<T, PoseTracksSource>) {
            detail::hash_animated_value(h, s.position);
            detail::hash_animated_value(h, s.rotation);
            detail::hash_animated_value(h, s.target);
            detail::hash_animated_value(h, s.zoom);
            detail::hash_animated_value(h, s.fov_deg);
            detail::hash_animated_value(h, s.focus_distance);
            detail::hash_animated_value(h, s.aperture);
            detail::hash_animated_value(h, s.max_blur);
            h.mix_bool(s.use_target);
        } else if constexpr (std::is_same_v<T, OrbitMotion>) {
            detail::hash_animated_value(h, s.target);
            detail::hash_animated_value(h, s.yaw);
            detail::hash_animated_value(h, s.pitch);
            detail::hash_animated_value(h, s.radius);
            detail::hash_animated_value(h, s.track);
            detail::hash_animated_value(h, s.dolly);
            detail::hash_animated_value(h, s.roll);
        } else if constexpr (std::is_same_v<T, TrajectoryMotion>) {
            h.mix_bool(s.use_arc_length);
            if (s.trajectory) {
                h.mix_u64(s.trajectory->size());
                for (const auto& p : s.trajectory->points()) {
                    h.mix_vec3(p.position);
                    h.mix_vec3(p.handle_in);
                    h.mix_vec3(p.handle_out);
                    h.mix_bool(p.target.has_value());
                    if (p.target) h.mix_vec3(*p.target);
                    h.mix_f32(p.roll_deg);
                }
                for (const auto& seg : s.trajectory->segments()) {
                    h.mix_u8(static_cast<std::uint8_t>(seg.kind));
                    h.mix_u64(seg.from_idx);
                    h.mix_u64(seg.to_idx);
                    h.mix_f32(seg.duration_frames);
                }
            } else {
                h.mix_u64(0xffffffffffffffffULL);
            }
        } else if constexpr (std::is_same_v<T, RegisteredMotionRef>) {
            h.mix_str(s.id);
        }
    }, desc.source);

    // ── orientation variant ────────────────────────────────────────
    h.mix_u8(desc.orientation.index());
    std::visit([&h](const auto& o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, FixedOrientation>) {
            // No fields.
        } else if constexpr (std::is_same_v<T, LookAtPoint>) {
            h.mix_vec3(o.target);
        } else if constexpr (std::is_same_v<T, LookAtLayer>) {
            h.mix_str(o.target);
        } else if constexpr (std::is_same_v<T, OrientAlongPath>) {
            h.mix_bool(o.keep_horizon);
        }
    }, desc.orientation);

    // ── modifiers ─────────────────────────────────────────────────
    h.mix_u64(desc.modifiers.size());
    for (const auto& m : desc.modifiers) {
        h.mix_u8(m.index());
        std::visit([&h](const auto& mod) {
            using T = std::decay_t<decltype(mod)>;
            if constexpr (std::is_same_v<T, IdleOscillation>) {
                h.mix_vec3(mod.position_amplitude);
                h.mix_vec3(mod.rotation_amplitude_deg);
                h.mix_f32(mod.zoom_amplitude);
                h.mix_f32(mod.frequency_hz);
                h.mix_f32(mod.phase);
            }
        }, m);
    }

    // ── constraints ───────────────────────────────────────────────
    h.mix_u64(desc.constraints.size());
    for (const auto& c : desc.constraints) {
        h.mix_u8(c.index());
        std::visit([&h](const auto& cs) {
            using T = std::decay_t<decltype(cs)>;
            if constexpr (std::is_same_v<T, LookAtConstraint>) {
                h.mix_vec3(cs.target);
            } else if constexpr (std::is_same_v<T, KeepHorizonConstraint>) {
                // No fields.
            } else if constexpr (std::is_same_v<T, DampedFollowConstraint>) {
                h.mix_f32(cs.damping);
            } else if constexpr (std::is_same_v<T, DistanceConstraint>) {
                h.mix_f32(cs.min_distance);
                h.mix_f32(cs.max_distance);
            } else if constexpr (std::is_same_v<T, RotationLimitConstraint>) {
                h.mix_f32(cs.max_pitch_deg);
                h.mix_f32(cs.max_yaw_deg);
                h.mix_f32(cs.max_roll_deg);
            }
        }, c);
    }

    return h.h;
}

} // namespace chronon3d::camera_v1
