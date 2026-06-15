#pragma once

// ── QuaternionTrack — Quaternion animation with slerp / squad ───────────────
//
// After Effects interpolates rotation using quaternion spherical linear
// interpolation (slerp) when the layer uses 3D orientation.  This module
// provides a keyframed quaternion track with:
//
//   * slerp  — spherical linear interpolation (shortest path)
//   * slerp with shortest-path flip detection
//   * easing applied to the interpolation parameter
//   * double-precision sub-frame evaluation (SampleTime)
//
// Usage:
//   AnimatedQuat rot;
//   rot.key(Frame{0},  glm::quat_identity<float, glm::defaultp>())
//      .key(Frame{60}, glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}),
//           EasingCurve{Easing::InOutCubic});
//   Quat q = rot.evaluate(Frame{30});
//
// Integration with AnimatedTransform:
//   AnimatedTransform now stores `rotation` as AnimatedQuat instead of
//   AnimatedValue<Vec3>.  The evaluate() method returns rotation as a
//   Quat — bypassing the Euler interpolation path entirely.

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chronon3d {

// ── Quaternion keyframe ─────────────────────────────────────────────────────
// Stores a target quaternion and the easing curve used to reach it from the
// previous keyframe.  Sub-frame precision via SampleTime.

struct QuatKeyframe {
    SampleTime  time{};              // sub-frame-precise position
    Quat        value{1.0f, 0.0f, 0.0f, 0.0f};  // target orientation
    EasingCurve easing{Easing::Linear};

    constexpr QuatKeyframe() = default;
    constexpr QuatKeyframe(SampleTime t, Quat v, EasingCurve e = EasingCurve{Easing::Linear})
        : time(t), value(v), easing(e) {}

    // For sorting
    bool operator<(const QuatKeyframe& other) const { return time.frame < other.time.frame; }
    bool operator<(double f) const { return time.frame < f; }
};

// ── AnimatedQuat — quaternion track with slerp ──────────────────────────────
// Provides a keyframe-based quaternion animation track that uses spherical
// linear interpolation (slerp) with easing.  Automatically picks the
// shortest rotation path (flips destination quaternion sign when needed).

class AnimatedQuat {
public:
    AnimatedQuat() = default;
    explicit AnimatedQuat(Quat default_value)
        : m_default_value(default_value) {}

    // ── Builder ──────────────────────────────────────────────────────────────

    /// Add a keyframe at an integer frame.
    AnimatedQuat& key(Frame frame, Quat value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        return key(SampleTime::from_frame_int(frame, FrameRate{30, 1}), value, easing);
    }

    /// Add a keyframe at a precise SampleTime (sub-frame precision).
    AnimatedQuat& key(SampleTime time, Quat value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        m_keyframes.emplace_back(time, value, easing);
        m_sorted = false;
        return *this;
    }

    /// Set a constant value (clears all keyframes).
    AnimatedQuat& set(const Quat& value) {
        m_keyframes.clear();
        m_default_value = value;
        m_sorted = true;
        return *this;
    }

    AnimatedQuat& operator=(const Quat& value) {
        return set(value);
    }

    // ── Evaluation ───────────────────────────────────────────────────────────

    /// Sub-frame evaluation (primary entry point).
    [[nodiscard]] Quat evaluate(SampleTime time) const {
        return evaluate_double(time.frame);
    }

    /// Integer-frame evaluation (backward compatible).
    [[nodiscard]] Quat evaluate(Frame frame) const {
        return evaluate_double(static_cast<double>(frame));
    }

    // ── Queries ──────────────────────────────────────────────────────────────

    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }
    [[nodiscard]] bool empty() const { return m_keyframes.empty(); }
    [[nodiscard]] std::size_t size() const { return m_keyframes.size(); }

    [[nodiscard]] bool is_time_dependent() const { return is_animated(); }

    void clear() { m_keyframes.clear(); m_sorted = true; }

    /// Shift every keyframe forward (or backward) by offset frames.
    void shift(Frame offset) {
        for (auto& kf : m_keyframes) {
            kf.time = SampleTime::from_frame(
                kf.time.frame + static_cast<double>(offset),
                kf.time.frame_rate);
        }
    }

private:
    // ── Slerp helpers ────────────────────────────────────────────────────────

    /// Spherical linear interpolation with shortest-path detection.
    /// Automatically flips the destination quaternion if the dot product
    /// is negative, ensuring the shortest rotation arc.
    [[nodiscard]] static Quat slerp_shortest(const Quat& a, const Quat& b, f32 t) {
        Quat dest = b;
        f32 dot = glm::dot(a, dest);
        if (dot < 0.0f) {
            dest = -dest;
            dot = -dot;
        }
        return glm::slerp(a, dest, t);
    }

    // ── Double-precision evaluation engine ──────────────────────────────────

    [[nodiscard]] Quat evaluate_double(double frame) const {
        if (m_keyframes.empty()) {
            return m_default_value;
        }

        ensure_sorted();

        const double start_f = m_keyframes.front().time.frame;
        const double end_f   = m_keyframes.back().time.frame;

        if (frame <= start_f) return m_keyframes.front().value;
        if (frame >= end_f)   return m_keyframes.back().value;

        // Find segment containing this frame (binary search)
        auto it = std::lower_bound(m_keyframes.begin(), m_keyframes.end(), frame,
            [](const QuatKeyframe& kf, double f) {
                return kf.time.frame < f;
            });

        if (it == m_keyframes.begin()) return it->value;
        if (it == m_keyframes.end())   return m_keyframes.back().value;

        const auto& prev = *std::prev(it);
        const auto& next = *it;

        const double prev_f = prev.time.frame;
        const double next_f = next.time.frame;
        if (next_f <= prev_f) return prev.value;

        const f32 raw_t = static_cast<f32>((frame - prev_f) / (next_f - prev_f));
        const f32 eased_t = prev.easing.apply(std::clamp(raw_t, 0.0f, 1.0f));

        return slerp_shortest(prev.value, next.value, eased_t);
    }

    void ensure_sorted() const {
        if (!m_sorted) {
            std::stable_sort(m_keyframes.begin(), m_keyframes.end());
            m_sorted = true;
        }
    }

    Quat m_default_value{1.0f, 0.0f, 0.0f, 0.0f};
    mutable std::vector<QuatKeyframe> m_keyframes;
    mutable bool m_sorted{true};
};

// ── Utility: quaternion from Euler degrees (matching Camera2_5D convention) ─
// After Effects / Chronon3D convention: x=pitch, y=yaw, z=roll (degrees).
inline Quat quat_from_euler_degrees(Vec3 euler_deg) {
    return glm::quat(glm::radians(euler_deg));
}

} // namespace chronon3d
