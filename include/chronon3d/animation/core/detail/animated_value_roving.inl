// ==============================================================================
// include/chronon3d/animation/core/detail/animated_value_roving.inl
//
// ROVING side of AnimatedValue<T> — extracted verbatim from
// include/chronon3d/animation/core/animated_value.hpp as part of the
// Phase-3 mechanical split.  Single source of truth for the constant-
// velocity roving keyframe time-redistribution algorithm.
//
// PRECONDITIONS
//   This `.inl` is included at the BOTTOM of animated_value.hpp, AFTER
//   the AnimatedValue<T> class body.  The template class type MUST be
//   fully defined at this point.
//
// CONTRACT
//   The body of `compute_roving()` is byte-equivalent to the original
//   (verbatim move + template<class>:: scope prefix added for out-of-line
//   definition).  Behaviour preserved end-to-end: behaviour invariants
//   (first/last keyframes cannot be roving; roving-with-no-anchors
//   frames are flipped to safe positions; re-sort after frame mods).
//
// Algorithm: for each group of consecutive roving keyframes between two
// non-roving anchors, redistributes their frames so the velocity between
// anchors is constant.  Constant-velocity per-anchor-pair guarantees a
// visually-smooth ramp irrespective of the author's frame choice.
// ==============================================================================

template <AnimatableValue T>
void AnimatedValue<T>::compute_roving() const {
    if (!m_roving_dirty) return;
    if (m_keyframes.size() < 3) {
        // Enforce: first and last keyframes cannot be roving.
        if (!m_keyframes.empty()) {
            m_keyframes.front().roving = false;
            m_keyframes.back().roving = false;
        }
        m_roving_dirty = false;
        return;
    }

    // Auto-compute bezier tangents before roving (like AnimationCurve).
    compute_auto_beziers();

    std::sort(m_keyframes.begin(), m_keyframes.end());

    size_t i = 0;
    while (i < m_keyframes.size()) {
        if (!m_keyframes[i].roving) { ++i; continue; }

        // Find left anchor (closest non-roving to the left)
        size_t left = (i > 0) ? (i - 1) : 0;
        while (left > 0 && m_keyframes[left].roving) --left;
        if (m_keyframes[left].roving) {
            m_keyframes[i].roving = false;
            ++i;
            continue;
        }

        // Find right anchor (next non-roving to the right)
        size_t right = i;
        while (right < m_keyframes.size() && m_keyframes[right].roving) ++right;
        if (right >= m_keyframes.size()) {
            for (size_t j = i; j < m_keyframes.size(); ++j)
                m_keyframes[j].roving = false;
            break;
        }

        // Constant velocity between left and right anchors
        const f32 t_left = static_cast<f32>(m_keyframes[left].frame);
        const f32 t_right = static_cast<f32>(m_keyframes[right].frame);
        const f32 dt = t_right - t_left;

        if constexpr (std::is_arithmetic_v<T>) {
            const f32 v_left = static_cast<f32>(m_keyframes[left].value);
            const f32 v_right = static_cast<f32>(m_keyframes[right].value);
            const f32 dv = v_right - v_left;

            if (std::abs(dv) < 1e-7f || dt <= 0.0f) {
                const size_t count = right - left - 1;
                for (size_t j = 0; j < count; ++j) {
                    f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                    m_keyframes[left + 1 + j].frame =
                        Frame{static_cast<i32>(std::round(t_left + frac * dt))};
                }
            } else {
                const f32 velocity = dv / dt;
                for (size_t j = left + 1; j < right; ++j) {
                    const f32 val_dist = static_cast<f32>(m_keyframes[j].value) - v_left;
                    const f32 target = t_left + val_dist / velocity;
                    m_keyframes[j].frame = Frame{static_cast<i32>(
                        std::round(std::clamp(target, t_left + 1.0f, t_right - 1.0f))
                    )};
                }
            }
        } else if constexpr (is_glm_vec_type_v<T>) {
            // Spatial types (Vec3, etc.): roving based on spatial distance
            const f32 total_dist = glm::length(m_keyframes[right].value - m_keyframes[left].value);
            if (total_dist < 1e-7f || dt <= 0.0f) {
                const size_t count = right - left - 1;
                for (size_t j = 0; j < count; ++j) {
                    f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                    m_keyframes[left + 1 + j].frame =
                        Frame{static_cast<i32>(std::round(t_left + frac * dt))};
                }
            } else {
                for (size_t j = left + 1; j < right; ++j) {
                    const f32 dist_from_left = glm::length(m_keyframes[j].value - m_keyframes[left].value);
                    const f32 frac = dist_from_left / total_dist;
                    const f32 target = t_left + frac * dt;
                    m_keyframes[j].frame = Frame{static_cast<i32>(
                        std::round(std::clamp(target, t_left + 1.0f, t_right - 1.0f))
                    )};
                }
            }
        } else {
            // Non-arithmetic, non-spatial T: distribute frames evenly
            const size_t count = right - left - 1;
            for (size_t j = 0; j < count; ++j) {
                f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                m_keyframes[left + 1 + j].frame =
                    Frame{static_cast<i32>(std::round(t_left + frac * dt))};
            }
        }

        i = right + 1;
    }

    m_roving_dirty = false;

    // Re-sort after frame modifications to maintain sorted invariant
    std::sort(m_keyframes.begin(), m_keyframes.end());
}
