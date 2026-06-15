#pragma once

// ── QuaternionTrack — Quaternion animation with slerp / squad ───────────────
//
// After Effects interpolates rotation using quaternion spherical linear
// interpolation (slerp) when the layer uses 3D orientation.  This module
// provides a keyframed quaternion track with:
//
//   * slerp  — spherical linear interpolation (shortest path)
//   * slerp with shortest-path flip detection
//   * KeyInterp modes: Linear, Bezier (temporal handles), Hold
//   * AutoBezier — auto-computed temporal tangents from neighbors
//   * Temporal handles (Tangent with dx/dy) for per-keyframe ease control
//   * EasingCurve fallback when no explicit temporal handles are set
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

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chronon3d {

// Reuse KeyInterp and Tangent from animation_curve for temporal handle control.
enum class QuatKeyInterp {
    Linear,      // straight slerp between keyframes (default)
    Bezier,      // custom temporal handles (in/out tangent)
    AutoBezier,  // auto-computed temporal tangents from neighbors
    Hold         // step function — value jumps at keyframe
};

// ── Quaternion keyframe ─────────────────────────────────────────────────────
// Stores a target quaternion and temporal interpolation controls.
// Sub-frame precision via SampleTime.

struct QuatKeyframe {
    SampleTime  time{};              // sub-frame-precise position
    Quat        value{1.0f, 0.0f, 0.0f, 0.0f};  // target orientation
    QuatKeyInterp interp{QuatKeyInterp::Linear};

    // Temporal handles (like AnimationCurve::Tangent).
    // dx = time offset in frames from keyframe
    // dy = value offset (applies to slerp progress, in [0,1] space)
    f32 in_dx{0.0f};
    f32 in_dy{0.0f};
    f32 out_dx{0.0f};
    f32 out_dy{0.0f};

    // Legacy easing fallback (used when interp == Linear and tangents are zero).
    EasingCurve easing{Easing::Linear};

    constexpr QuatKeyframe() = default;
    constexpr QuatKeyframe(SampleTime t, Quat v, EasingCurve e = EasingCurve{Easing::Linear})
        : time(t), value(v), interp(QuatKeyInterp::Linear), easing(e) {}

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
        m_auto_bezier_dirty = false;
        return *this;
    }

    /// Add a keyframe with temporal handle control (like AnimationCurve).
    /// @param interp  Interpolation mode (Linear, Bezier, AutoBezier, Hold)
    /// @param in_dx   Incoming tangent: time offset from keyframe (frames, negative = backward)
    /// @param in_dy   Incoming tangent: value offset (slerp progress, in [0,1] space)
    /// @param out_dx  Outgoing tangent: time offset from keyframe (positive = forward)
    /// @param out_dy  Outgoing tangent: value offset
    AnimatedQuat& key(Frame frame, Quat value, QuatKeyInterp interp,
                       f32 in_dx, f32 in_dy, f32 out_dx, f32 out_dy) {
        QuatKeyframe kf(SampleTime::from_frame_int(frame, FrameRate{30, 1}), value);
        kf.interp = interp;
        kf.in_dx = in_dx;
        kf.in_dy = in_dy;
        kf.out_dx = out_dx;
        kf.out_dy = out_dy;
        m_keyframes.push_back(kf);
        m_sorted = false;
        if (interp == QuatKeyInterp::AutoBezier) m_auto_bezier_dirty = true;
        return *this;
    }

    /// Set a constant value (clears all keyframes).
    AnimatedQuat& set(const Quat& value) {
        m_keyframes.clear();
        m_default_value = value;
        m_sorted = true;
        m_auto_bezier_dirty = false;
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

    void clear() { m_keyframes.clear(); m_sorted = true; m_auto_bezier_dirty = false; }

    /// Auto-compute temporal bezier handles based on adjacent keyframes.
    void compute_auto_beziers() const {
        if (!m_auto_bezier_dirty) return;
        ensure_sorted();

        if (m_keyframes.size() < 3) { m_auto_bezier_dirty = false; return; }

        for (size_t i = 0; i < m_keyframes.size(); ++i) {
            auto& kf = m_keyframes[i];
            if (kf.interp != QuatKeyInterp::AutoBezier) continue;

            const bool has_prev = (i > 0);
            const bool has_next = (i + 1 < m_keyframes.size());

            constexpr f32 kTension = 1.0f / 3.0f;

            if (has_prev && has_next) {
                const f32 dt_prev = static_cast<f32>(kf.time.frame - m_keyframes[i - 1].time.frame);
                const f32 dt_next = static_cast<f32>(m_keyframes[i + 1].time.frame - kf.time.frame);
                // In slerp space, the "value" is the quaternion angular distance.
                // Compute approximate angular velocity from neighbors.
                const Quat& q_prev = m_keyframes[i - 1].value;
                const Quat& q_next = m_keyframes[i + 1].value;
                f32 dot_prev = glm::dot(kf.value, q_prev);
                f32 dot_next = glm::dot(kf.value, q_next);
                if (dot_prev < 0.0f) dot_prev = -dot_prev;
                if (dot_next < 0.0f) dot_next = -dot_next;
                const f32 angle_prev = 2.0f * std::acos(std::clamp(dot_prev, 0.0f, 1.0f));
                const f32 angle_next = 2.0f * std::acos(std::clamp(dot_next, 0.0f, 1.0f));

                const f32 slope = (angle_prev + angle_next) / (dt_prev + dt_next);
                kf.in_dx  = -dt_prev * kTension;
                kf.in_dy  = -slope * dt_prev * kTension;
                kf.out_dx = dt_next * kTension;
                kf.out_dy = slope * dt_next * kTension;
            } else if (has_prev) {
                kf.in_dx  = 0.0f; kf.in_dy  = 0.0f;
                kf.out_dx = 0.0f; kf.out_dy = 0.0f;
            } else if (has_next) {
                kf.in_dx  = 0.0f; kf.in_dy  = 0.0f;
                kf.out_dx = 0.0f; kf.out_dy = 0.0f;
            }
        }
        m_auto_bezier_dirty = false;
    }

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

    // ── Temporal bezier evaluation ───────────────────────────────────────────
    // Given a local time t ∈ [0,1] and the previous keyframe as the source
    // of temporal handles, returns the effective slerp parameter ∈ [0,1].
    [[nodiscard]] static f32 eval_temporal_bezier(
        const QuatKeyframe& prev, const QuatKeyframe& next, f32 t
    ) {
        if (prev.interp == QuatKeyInterp::Hold) return 0.0f;

        // Compute the effective duration in frames between the keyframes.
        const f32 duration = static_cast<f32>(next.time.frame - prev.time.frame);
        if (duration <= 0.0f) return 0.0f;

        if (prev.interp == QuatKeyInterp::Bezier || prev.interp == QuatKeyInterp::AutoBezier) {
            // Out-tangent from prev keyframe
            const f32 p1x = std::clamp(prev.out_dx / duration, 0.0f, 1.0f);
            const f32 p1y = prev.out_dy;
            // In-tangent from next keyframe
            const f32 p2x = std::clamp(1.0f + prev.in_dx / duration, 0.0f, 1.0f);
            const f32 p2y = next.in_dy;

            // Cubic bezier: x(t) maps time, y(t) maps slerp progress
            const f32 cx = 3.0f * p1x;
            const f32 bx = 3.0f * (p2x - p1x) - cx;
            const f32 ax = 1.0f - cx - bx;

            // Newton-Raphson to find u such that x(u) == t
            auto sample_x = [&](f32 u) { return ((ax * u + bx) * u + cx) * u; };
            auto sample_y = [&](f32 u) { return ((3.0f*p1y + 3.0f*(p2y-2.0f*p1y)*u + (1.0f-3.0f*p1y-3.0f*(p2y-2.0f*p1y))*u*u)*u); };

            f32 u = t;
            for (int i = 0; i < 8; ++i) {
                f32 x_val = sample_x(u) - t;
                if (std::abs(x_val) < 1e-6f) {
                    f32 result = sample_y(u);
                    return std::clamp(result, 0.0f, 1.0f);
                }
                const f32 dVal = (3.0f * ax * u + 2.0f * bx) * u + cx;
                if (std::abs(dVal) < 1e-6f) break;
                u -= x_val / dVal;
            }
            // Bisection fallback
            f32 lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 16; ++i) {
                f32 x_val = sample_x(u);
                if (std::abs(x_val - t) < 1e-6f) return std::clamp(sample_y(u), 0.0f, 1.0f);
                if (t > x_val) lo = u; else hi = u;
                u = (lo + hi) * 0.5f;
            }
            return std::clamp(sample_y(u), 0.0f, 1.0f);
        }

        // Linear: use easing curve
        return prev.easing.apply(std::clamp(t, 0.0f, 1.0f));
    }

    // ── Double-precision evaluation engine ──────────────────────────────────

    [[nodiscard]] Quat evaluate_double(double frame) const {
        if (m_keyframes.empty()) {
            return m_default_value;
        }

        ensure_sorted();

        // Auto-compute auto-bezier handles if dirty.
        if (m_auto_bezier_dirty) {
            compute_auto_beziers();
        }

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
        const f32 eased_t = eval_temporal_bezier(prev, next, raw_t);

        if (prev.interp == QuatKeyInterp::Hold) {
            return prev.value;
        }

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
    mutable bool m_auto_bezier_dirty{false};
};

// ── Utility: quaternion from Euler degrees (matching Camera2_5D convention) ─
// After Effects / Chronon3D convention: x=pitch, y=yaw, z=roll (degrees).
inline Quat quat_from_euler_degrees(Vec3 euler_deg) {
    return glm::quat(glm::radians(euler_deg));
}

} // namespace chronon3d
