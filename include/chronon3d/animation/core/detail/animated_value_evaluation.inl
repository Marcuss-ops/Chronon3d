# ==============================================================================
# include/chronon3d/animation/core/detail/animated_value_evaluation.inl
#
# EVALUATION side of AnimatedValue<T> — extracted verbatim from
# include/chronon3d/animation/core/animated_value.hpp as part of the
# Phase-3 mechanical split.  Single source of truth for the core
# interpolation engine (boilerplate-free, depends only on T being
# AnimatableValue).
#
# PRECONDITIONS
#   This `.inl` is included at the BOTTOM of animated_value.hpp, AFTER
#   the AnimatedValue<T> class body.  The template class type MUST be
#   fully defined at this point; that's why the .inl lives in `detail/`
#   (must include animated_value.hpp first, which includes its detail/*
#   blocks at the very end).
#
# CONTRACT
#   The bodies of `evaluate_base_double(...)`, `has_spatial_handles(...)`,
#   and `should_cache_double(...)` are byte-equivalent to the original
#   lines in animated_value.hpp (verbatim move + template<class>:: scope
#   prefix added for out-of-line definitions).
#
# NOTE on the LoopMode block
#   `evaluate_base_double(...)` carries a small `if (m_loop_mode == LoopMode::Loop)`
#   block and a corresponding PingPong block.  These are KEPT INSIDE
#   `evaluate_base_double(...)` rather than extracted into a dedicated
#   `animated_value_looping.inl`.  Cutting a `LoopMode` block out of the
#   middle of a function body via `#include` is an archaic C-style
#   anti-pattern that breaks IDE formatting and scope analysis (see
#   docs/ARCHIVE/anti-loop-include-pattern for example discussion).
#   Inline preservation is also what `git blame` + diff viewers expect.
# ==============================================================================

// has_spatial_handles — checks a keyframe for non-zero bezier handles.
/// Returns true if the keyframe has non-zero out_handle or in_handle
/// (spatial bezier mode).  Vector types only.
template <AnimatableValue T>
[[nodiscard]] bool AnimatedValue<T>::has_spatial_handles(const Keyframe<T>& kf) {
    if constexpr (std::is_same_v<T, Vec3> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec4>) {
        return glm::length2(kf.out_handle) > 1e-12f || glm::length2(kf.in_handle) > 1e-12f;
    }
    return false;
}

// ── evaluate_base_double — core interpolation engine ───────────────────────
template <AnimatableValue T>
[[nodiscard]] T AnimatedValue<T>::evaluate_base_double(double frame) const {
    // Auto-compute beziers and roving before evaluation if dirty.
    if (m_auto_bezier_dirty) {
        compute_auto_beziers();
    }
    if (m_roving_dirty) {
        compute_roving();
    }
    if (m_keyframes.empty()) {
        return m_default_value;
    }

    const double start_f = static_cast<double>(m_keyframes.front().frame);
    const double end_f   = static_cast<double>(m_keyframes.back().frame);
    const double range   = end_f - start_f;

    double eval_frame = frame;

    if (m_loop_mode == LoopMode::Loop && range > 0) {
        if (frame < start_f) {
            eval_frame = end_f - std::fmod(start_f - frame, range);
        } else {
            eval_frame = start_f + std::fmod(frame - start_f, range);
        }
    } else if (m_loop_mode == LoopMode::PingPong && range > 0) {
        double t = (frame < start_f) ? (start_f - frame) : (frame - start_f);
        double cycle = std::floor(t / range);
        double offset = std::fmod(t, range);
        if (static_cast<int64_t>(cycle) % 2 == 0) {
            eval_frame = start_f + offset;
        } else {
            eval_frame = end_f - offset;
        }
    } else {
        if (frame <= start_f) return m_keyframes.front().value;
        if (frame >= end_f)   return m_keyframes.back().value;
    }

    // Linear scan for keyframe segment (uses > to correctly handle
    // duplicate frames — matches upper_bound behavior).
    size_t idx = 0;
    for (size_t i = 0; i + 1 < m_keyframes.size(); ++i) {
        if (static_cast<double>(m_keyframes[i + 1].frame) > eval_frame) {
            idx = i;
            break;
        }
        idx = i + 1;
    }
    if (idx + 1 >= m_keyframes.size()) {
        return m_keyframes.back().value;
    }

    const auto& prev = m_keyframes[idx];
    const auto& next = m_keyframes[idx + 1];

    const double prev_f = static_cast<double>(prev.frame);
    const double next_f = static_cast<double>(next.frame);
    if (next_f <= prev_f) return prev.value;

    const f32 t = static_cast<f32>((eval_frame - prev_f) / (next_f - prev_f));

    // Hold interpolation: value jumps at keyframe.
    if (prev.interp == InterpMode::Hold) return prev.value;

    // Temporal bezier interpolation (arithmetic/scalar types).
    if constexpr (std::is_arithmetic_v<T>) {
        if (prev.interp == InterpMode::Bezier || prev.interp == InterpMode::AutoBezier) {
            return eval_temporal_bezier(prev, next, t);
        }
    }

    // Spatial bezier path: when handles are present, use CubicBezier3D
    // for smooth curved interpolation through 3+ points.
    if constexpr (std::is_same_v<T, Vec3> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec4>) {
        if (has_spatial_handles(prev) || has_spatial_handles(next)) {
            const f32 eased_t = prev.easing.apply(std::clamp(t, 0.0f, 1.0f));
            // Build CubicBezier3D from the four control points
            const Vec3 p0 = prev.value;
            const Vec3 p1 = prev.value + prev.out_handle;
            const Vec3 p3 = next.value;
            const Vec3 p2 = next.value + next.in_handle;
            // De Casteljau evaluation
            const f32 u = 1.0f - eased_t;
            return u * u * u * p0
                 + 3.0f * u * u * eased_t * p1
                 + 3.0f * u * eased_t * eased_t * p2
                 + eased_t * eased_t * eased_t * p3;
        }
    }

    return interpolate_values(prev.value, next.value, std::clamp(t, 0.0f, 1.0f), prev.easing);
}

// should_cache_double — shared cache-check for Frame + SampleTime paths.
template <AnimatableValue T>
[[nodiscard]] bool AnimatedValue<T>::should_cache_double(double frame) const {
    if (has_expression()) return true;
    if (m_keyframes.empty()) return false;

    const double start_f = static_cast<double>(m_keyframes.front().frame);
    const double end_f   = static_cast<double>(m_keyframes.back().frame);

    if (frame <= start_f || frame >= end_f) return false;
    return true;
}
