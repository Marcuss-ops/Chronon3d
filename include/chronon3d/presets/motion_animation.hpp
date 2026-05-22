#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/animation/easing.hpp>
#include <chronon3d/presets/motion_state.hpp>
#include <functional>
#include <algorithm>
#include <cmath>

namespace chronon3d::presets::motion {

struct AnimationContext {
    Frame current_frame;
    Frame total_duration;
    f32 fps{30.0f};
};

// Remotion-style spring physical solver (analytical solution to damped harmonic oscillator)
inline f32 compute_spring(f32 t, f32 mass, f32 stiffness, f32 damping) {
    if (t <= 0.0f) return 0.0f;

    f32 m = mass;
    f32 k = stiffness;
    f32 c = damping;

    if (m <= 0.0f) m = 1.0f;
    if (k <= 0.0f) k = 100.0f;
    if (c < 0.0f) c = 0.0f;

    f32 omega0 = std::sqrt(k / m);
    f32 zeta = c / (2.0f * std::sqrt(m * k));

    if (zeta < 1.0f) {
        // Underdamped
        f32 alpha = zeta * omega0;
        f32 omega_d = omega0 * std::sqrt(1.0f - zeta * zeta);
        f32 envelope = std::exp(-alpha * t);
        return 1.0f - envelope * (std::cos(omega_d * t) + (alpha / omega_d) * std::sin(omega_d * t));
    } else if (std::abs(zeta - 1.0f) < 1e-5f) {
        // Critically damped
        f32 envelope = std::exp(-omega0 * t);
        return 1.0f - envelope * (1.0f + omega0 * t);
    } else {
        // Overdamped
        f32 alpha = c / (2.0f * m);
        f32 beta = omega0 * std::sqrt(zeta * zeta - 1.0f);
        f32 r1 = -alpha + beta;
        f32 r2 = -alpha - beta;
        f32 r_diff = r1 - r2;
        if (std::abs(r_diff) < 1e-5f) {
            f32 envelope = std::exp(-omega0 * t);
            return 1.0f - envelope * (1.0f + omega0 * t);
        }
        return 1.0f + (r2 / r_diff) * std::exp(r1 * t) - (r1 / r_diff) * std::exp(r2 * t);
    }
}

class MotionAnimation {
public:
    using Modifier = std::function<void(MotionState&, f32 progress)>;

    MotionAnimation(Frame start, Frame end, EasingCurve easing, Modifier modifier)
        : m_start(start), m_end(end), m_easing(easing), m_modifier(modifier) {}

    void apply(const AnimationContext& ctx, MotionState& state) const {
        if (ctx.current_frame < m_start) return;

        // Clamping style Remotion (extrapolateRight: "clamp")
        Frame clamped_frame = std::min(ctx.current_frame, m_end);
        
        f32 linear_progress = 0.0f;
        if (m_end > m_start) {
            linear_progress = static_cast<f32>(clamped_frame - m_start) / static_cast<f32>(m_end - m_start);
        } else {
            linear_progress = 1.0f;
        }

        // Apply Easing
        f32 eased_progress = m_easing.apply(linear_progress);

        // Apply Modifier
        if (m_modifier) {
            m_modifier(state, eased_progress);
        }
    }

    [[nodiscard]] Frame start() const { return m_start; }
    [[nodiscard]] Frame end() const { return m_end; }
    [[nodiscard]] EasingCurve easing() const { return m_easing; }

private:
    Frame m_start;
    Frame m_end;
    EasingCurve m_easing;
    Modifier m_modifier;
};

} // namespace chronon3d::presets::motion
