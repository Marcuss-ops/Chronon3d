#include <chronon3d/animation/core/animation_curve.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

// ── Implementation ───────────────────────────────────────────────────────────

void AnimationCurve::ensure_sorted() const {
    if (!m_sorted) {
        std::stable_sort(m_keyframes.begin(), m_keyframes.end());
        m_sorted = true;
    }
}

void AnimationCurve::compute_auto_beziers() const {
    ensure_sorted();

    for (size_t i = 0; i < m_keyframes.size(); ++i) {
        auto& kf = m_keyframes[i];
        if (kf.interp != KeyInterp::AutoBezier) continue;

        const bool has_prev = (i > 0);
        const bool has_next = (i + 1 < m_keyframes.size());

        if (has_prev && has_next) {
            constexpr f32 kTension = 1.0f / 3.0f;

            const f32 dt_prev = static_cast<f32>(kf.frame - m_keyframes[i - 1].frame);
            const f32 dt_next = static_cast<f32>(m_keyframes[i + 1].frame - kf.frame);
            const f32 dv_prev = kf.value - m_keyframes[i - 1].value;
            const f32 dv_next = m_keyframes[i + 1].value - kf.value;

            const f32 slope = (dv_prev + dv_next) / (dt_prev + dt_next);

            kf.in_tangent = Tangent(-dt_prev * kTension, -slope * dt_prev * kTension);
            kf.out_tangent = Tangent(dt_next * kTension, slope * dt_next * kTension);

        } else if (has_prev) {
            constexpr f32 kTension = 1.0f / 3.0f;
            const f32 dt = static_cast<f32>(kf.frame - m_keyframes[i - 1].frame);
            const f32 dv = kf.value - m_keyframes[i - 1].value;
            const f32 slope = (dt > 0.0f) ? (dv / dt) : 0.0f;

            kf.in_tangent = Tangent(-dt * kTension, -slope * dt * kTension);
            kf.out_tangent = Tangent(0.0f, 0.0f);

        } else if (has_next) {
            constexpr f32 kTension = 1.0f / 3.0f;
            const f32 dt = static_cast<f32>(m_keyframes[i + 1].frame - kf.frame);
            const f32 dv = m_keyframes[i + 1].value - kf.value;
            const f32 slope = (dt > 0.0f) ? (dv / dt) : 0.0f;

            kf.in_tangent = Tangent(0.0f, 0.0f);
            kf.out_tangent = Tangent(dt * kTension, slope * dt * kTension);
        }
    }
}

void AnimationCurve::compute_roving() const {
    if (!m_roving_dirty) return;
    ensure_sorted();

    // First pass: compute auto-beziers for any AutoBezier keyframes
    compute_auto_beziers();
    m_auto_bezier_dirty = false;

    // Second pass: for each group of consecutive roving keyframes between two
    // non-roving anchors, redistribute their frames for constant velocity.
    size_t i = 0;
    while (i < m_keyframes.size()) {
        if (!m_keyframes[i].roving) {
            ++i;
            continue;
        }

        // Find left anchor (closest non-roving to the left)
        size_t left_anchor = (i > 0) ? (i - 1) : 0;
        while (left_anchor > 0 && m_keyframes[left_anchor].roving) {
            --left_anchor;
        }
        if (m_keyframes[left_anchor].roving) {
            m_keyframes[i].roving = false;
            ++i;
            continue;
        }

        // Find right anchor (next non-roving to the right)
        size_t right_anchor = i;
        while (right_anchor < m_keyframes.size() && m_keyframes[right_anchor].roving) {
            ++right_anchor;
        }
        if (right_anchor >= m_keyframes.size()) {
            for (size_t j = i; j < m_keyframes.size(); ++j) {
                m_keyframes[j].roving = false;
            }
            break;
        }

        // Compute constant velocity between left and right anchors
        const f32 v_left = m_keyframes[left_anchor].value;
        const f32 v_right = m_keyframes[right_anchor].value;
        const f32 t_left = static_cast<f32>(m_keyframes[left_anchor].frame);
        const f32 t_right = static_cast<f32>(m_keyframes[right_anchor].frame);
        const f32 total_value_dist = v_right - v_left;
        const f32 total_time_dist = t_right - t_left;

        if (std::abs(total_value_dist) < 1e-7f || total_time_dist <= 0.0f) {
            // Zero value change: distribute frames evenly
            const size_t count = right_anchor - left_anchor - 1;
            for (size_t j = 0; j < count; ++j) {
                f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                m_keyframes[left_anchor + 1 + j].frame =
                    Frame{static_cast<i32>(std::round(t_left + frac * total_time_dist))};
            }
        } else {
            // Constant velocity: v(t) = v_left + (t - t_left) * velocity
            const f32 velocity = total_value_dist / total_time_dist;

            for (size_t j = left_anchor + 1; j < right_anchor; ++j) {
                const f32 value_dist = m_keyframes[j].value - v_left;
                const f32 target_time = t_left + value_dist / velocity;
                m_keyframes[j].frame = Frame{static_cast<i32>(
                    std::round(std::clamp(target_time, t_left + 1.0f, t_right - 1.0f))
                )};
            }
        }

        i = right_anchor + 1;
    }

    m_roving_dirty = false;
    m_sorted = false;  // frames may have changed
    ensure_sorted();
}

// Evaluate a cubic bezier segment between two keyframes.
f32 AnimationCurve::eval_bezier_segment(
    const AnimationKeyframe& prev, const AnimationKeyframe& next, f32 t)
{
    const f32 duration = static_cast<f32>(next.frame - prev.frame);
    if (duration <= 0.0f) return next.value;

    // Control points in (time_fraction, value) space
    const f32 p0y = prev.value;
    const f32 p3y = next.value;

    // Out-tangent from prev keyframe
    const f32 p1x = std::clamp(prev.out_tangent.dx / duration, 0.0f, 1.0f);
    const f32 p1y = prev.value + prev.out_tangent.dy;

    // In-tangent from next keyframe (negative dx because it goes backwards)
    const f32 p2x = std::clamp(1.0f + next.in_tangent.dx / duration, 0.0f, 1.0f);
    const f32 p2y = next.value + next.in_tangent.dy;

    // X coefficients (p0x=0, p3x=1, same as CubicBezier in easing.hpp)
    const f32 cx = 3.0f * p1x;
    const f32 bx = 3.0f * (p2x - p1x) - cx;
    const f32 ax = 1.0f - cx - bx;

    // Y coefficients — generalized for arbitrary p0y, p3y
    const f32 cy = 3.0f * (p1y - p0y);
    const f32 by = 3.0f * (p2y - 2.0f * p1y + p0y);
    const f32 ay = p3y - p0y - cy - by;

    auto sample_x = [&](f32 u) -> f32 {
        return ((ax * u + bx) * u + cx) * u;
    };

    auto sample_y = [&](f32 u) -> f32 {
        return ((ay * u + by) * u + cy) * u + p0y;
    };

    auto sample_dx = [&](f32 u) -> f32 {
        return (3.0f * ax * u + 2.0f * bx) * u + cx;
    };

    // Newton-Raphson to find u such that sample_x(u) == t
    f32 u = t;
    for (int i = 0; i < 8; ++i) {
        f32 x_val = sample_x(u) - t;
        if (std::abs(x_val) < 1e-6f) {
            return sample_y(u);
        }
        f32 dVal = sample_dx(u);
        if (std::abs(dVal) < 1e-6f) break;
        u -= x_val / dVal;
    }

    // Bisection fallback
    f32 lo = 0.0f;
    f32 hi = 1.0f;
    u = t;
    for (int i = 0; i < 16; ++i) {
        f32 x_val = sample_x(u);
        if (std::abs(x_val - t) < 1e-6f) return sample_y(u);
        if (t > x_val) lo = u; else hi = u;
        u = (lo + hi) * 0.5f;
    }

    return sample_y(u);
}

f32 AnimationCurve::eval_linear_segment(f32 v0, f32 v1, f32 t) {
    return v0 + (v1 - v0) * t;
}

// Core double-precision evaluation engine — used by all evaluate() overloads.
f32 AnimationCurve::evaluate_double(double frame) const {
    if (m_keyframes.empty()) {
        return m_default_value;
    }

    ensure_sorted();

    // Auto-compute roving before evaluation if dirty.
    if (m_roving_dirty) {
        compute_roving();
    }

    if (m_auto_bezier_dirty) {
        compute_auto_beziers();
        m_auto_bezier_dirty = false;
    }

    const double start_f = static_cast<double>(m_keyframes.front().frame);
    const double end_f   = static_cast<double>(m_keyframes.back().frame);

    if (frame <= start_f) return m_keyframes.front().value;
    if (frame >= end_f)   return m_keyframes.back().value;

    // Find the segment containing this frame (binary search with double comparison)
    auto it = std::lower_bound(m_keyframes.begin(), m_keyframes.end(), frame,
        [](const AnimationKeyframe& kf, double f) {
            return static_cast<double>(kf.frame) < f;
        });

    if (it == m_keyframes.begin()) return it->value;
    if (it == m_keyframes.end()) return m_keyframes.back().value;

    const auto& next = *it;
    const auto& prev = *std::prev(it);

    const double prev_f = static_cast<double>(prev.frame);
    const double next_f = static_cast<double>(next.frame);
    if (next_f <= prev_f) return prev.value;

    const f32 t = static_cast<f32>((frame - prev_f) / (next_f - prev_f));

    switch (prev.interp) {
        case KeyInterp::Hold:
            return prev.value;
        case KeyInterp::Linear:
            return eval_linear_segment(prev.value, next.value, t);
        case KeyInterp::Bezier:
        case KeyInterp::AutoBezier:
            return eval_bezier_segment(prev, next, t);
        default:
            return eval_linear_segment(prev.value, next.value, t);
    }
}

} // namespace chronon3d
