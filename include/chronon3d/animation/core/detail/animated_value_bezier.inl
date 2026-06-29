// ==============================================================================
// include/chronon3d/animation/core/detail/animated_value_bezier.inl
//
// BEZIER side of AnimatedValue<T> — extracted verbatim from
// include/chronon3d/animation/core/animated_value.hpp as part of the
// Phase-3 mechanical split.  Single source of truth for:
//   • `compute_auto_beziers()` — auto-tangent computation for
//     AutoBezier keyframes (1/3-tension interpolation, similar to
//     AnimationCurve::compute_auto_beziers).
//   • `eval_temporal_bezier(...)` — Newton-Raphson solve for
//     x(u) = t, return y(u) (the scalar temporal-bezier evaluation
//     used when prev.interp is Bezier/AutoBezier and T is arithmetic).
//
// PRECONDITIONS
//   This `.inl` is included at the BOTTOM of animated_value.hpp, AFTER
//   the AnimatedValue<T> class body.  The template class type MUST be
//   fully defined at this point.
//
// CONTRACT
//   Bodies byte-equivalent to the original lines in animated_value.hpp
//   (verbatim move + template<class>:: scope prefix added for
//   out-of-line definitions).  Algorithm invariants unchanged:
//     • No tangent updates at endpoints (kTension = 1/3; ends have
//       at most one neighbour).
//     • Newton-Raphson fallback to bisection after 8 iterations OR
//       |dVal| < 1e-6.
// ==============================================================================

// compute_auto_beziers — temporal-tangent auto-compute for AutoBezier keyframes.
template <AnimatableValue T>
void AnimatedValue<T>::compute_auto_beziers() const {
    if (!m_auto_bezier_dirty) return;
    if (m_keyframes.size() < 3) { m_auto_bezier_dirty = false; return; }

    for (size_t i = 0; i < m_keyframes.size(); ++i) {
        auto& kf = m_keyframes[i];
        if (kf.interp != InterpMode::AutoBezier) continue;

        const bool has_prev = (i > 0);
        const bool has_next = (i + 1 < m_keyframes.size());

        constexpr f32 kTension = 1.0f / 3.0f;

        if (has_prev && has_next) {
            const f32 dt_prev = static_cast<f32>(kf.frame - m_keyframes[i - 1].frame);
            const f32 dt_next = static_cast<f32>(m_keyframes[i + 1].frame - kf.frame);

            if constexpr (std::is_arithmetic_v<T>) {
                const f32 dv_prev = static_cast<f32>(kf.value - m_keyframes[i - 1].value);
                const f32 dv_next = static_cast<f32>(m_keyframes[i + 1].value - kf.value);
                const f32 slope = (dv_prev + dv_next) / (dt_prev + dt_next);

                kf.temporal_in_dx  = -dt_prev * kTension;
                kf.temporal_in_dy  = -slope * dt_prev * kTension;
                kf.temporal_out_dx = dt_next * kTension;
                kf.temporal_out_dy = slope * dt_next * kTension;
            } else {
                kf.temporal_in_dx = 0.0f;
                kf.temporal_in_dy = 0.0f;
                kf.temporal_out_dx = 0.0f;
                kf.temporal_out_dy = 0.0f;
            }
        } else if (has_prev) {
            if constexpr (std::is_arithmetic_v<T>) {
                const f32 dt = static_cast<f32>(kf.frame - m_keyframes[i - 1].frame);
                const f32 dv = static_cast<f32>(kf.value - m_keyframes[i - 1].value);
                const f32 slope = (dt > 0.0f) ? (dv / dt) : 0.0f;
                kf.temporal_in_dx  = -dt * kTension;
                kf.temporal_in_dy  = -slope * dt * kTension;
            }
            kf.temporal_out_dx = 0.0f;
            kf.temporal_out_dy = 0.0f;
        } else if (has_next) {
            if constexpr (std::is_arithmetic_v<T>) {
                const f32 dt = static_cast<f32>(m_keyframes[i + 1].frame - kf.frame);
                const f32 dv = static_cast<f32>(m_keyframes[i + 1].value - kf.value);
                const f32 slope = (dt > 0.0f) ? (dv / dt) : 0.0f;
                kf.temporal_out_dx = dt * kTension;
                kf.temporal_out_dy = slope * dt * kTension;
            }
            kf.temporal_in_dx = 0.0f;
            kf.temporal_in_dy = 0.0f;
        }
    }
    m_auto_bezier_dirty = false;
}

// ── eval_temporal_bezier — scalar-only temporal bezier evaluation ──────────
// Uses Newton-Raphson to solve x(u) = t, then returns y(u).
// Bisection fallback handled inside the body for non-converging cases.
template <AnimatableValue T>
[[nodiscard]] T AnimatedValue<T>::eval_temporal_bezier(
    const Keyframe<T>& prev, const Keyframe<T>& next, f32 t)
{
    const f32 duration = static_cast<f32>(next.frame - prev.frame);
    if (duration <= 0.0f) return next.value;

    const f32 p0y = static_cast<f32>(prev.value);
    const f32 p3y = static_cast<f32>(next.value);

    // Out-tangent from prev keyframe
    const f32 p1x = std::clamp(prev.temporal_out_dx / duration, 0.0f, 1.0f);
    const f32 p1y = p0y + prev.temporal_out_dy;

    // In-tangent from next keyframe
    const f32 p2x = std::clamp(1.0f + next.temporal_in_dx / duration, 0.0f, 1.0f);
    const f32 p2y = p3y + next.temporal_in_dy;

    // X coefficients
    const f32 cx = 3.0f * p1x;
    const f32 bx = 3.0f * (p2x - p1x) - cx;
    const f32 ax = 1.0f - cx - bx;

    // Y coefficients
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

    // Newton-Raphson
    f32 u = t;
    for (int i = 0; i < 8; ++i) {
        f32 x_val = sample_x(u) - t;
        if (std::abs(x_val) < 1e-6f)
            return static_cast<T>(sample_y(u));
        f32 dVal = sample_dx(u);
        if (std::abs(dVal) < 1e-6f) break;
        u -= x_val / dVal;
    }

    // Bisection fallback
    f32 lo = 0.0f, hi = 1.0f;
    u = t;
    for (int i = 0; i < 16; ++i) {
        f32 x_val = sample_x(u);
        if (std::abs(x_val - t) < 1e-6f)
            return static_cast<T>(sample_y(u));
        if (t > x_val) lo = u; else hi = u;
        u = (lo + hi) * 0.5f;
    }
    return static_cast<T>(sample_y(u));
}
