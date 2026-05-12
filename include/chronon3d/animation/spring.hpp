#pragma once

#include <chronon3d/core/time.hpp>
#include <chronon3d/timeline/sequence.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d {

struct SpringConfig {
    f32 stiffness{100.0f};
    f32 damping{15.0f};
    f32 mass{1.0f};
};

/**
 * Deterministic, closed-form spring animation.
 * Formula based on damped harmonic oscillator.
 */
inline f32 spring(f32 t, f32 from, f32 to, const SpringConfig& config = {}) {
    if (t <= 0.0f) return from;

    // Derived properties
    // omega_n = sqrt(stiffness / mass)
    // zeta = damping / (2 * sqrt(stiffness * mass))
    f32 omega_n = std::sqrt(config.stiffness / config.mass);
    f32 zeta = config.damping / (2.0f * std::sqrt(config.stiffness * config.mass));

    if (zeta < 1.0f) {
        // Underdamped
        f32 omega_d = omega_n * std::sqrt(1.0f - zeta * zeta);
        f32 envelope = std::exp(-zeta * omega_n * t);
        
        // x(t) = to - (to - from) * envelope * (cos(omega_d * t) + (zeta * omega_n / omega_d) * sin(omega_d * t))
        f32 sin_part = (zeta * omega_n / omega_d) * std::sin(omega_d * t);
        f32 cos_part = std::cos(omega_d * t);
        
        return to - (to - from) * envelope * (cos_part + sin_part);
    } else if (zeta > 1.0f) {
        // Overdamped
        f32 omega_r = omega_n * std::sqrt(zeta * zeta - 1.0f);
        f32 r1 = -zeta * omega_n + omega_r;
        f32 r2 = -zeta * omega_n - omega_r;
        
        f32 c2 = (to - from) * (r1 / (r2 - r1));
        f32 c1 = (to - from) - c2;
        
        return to - (c1 * std::exp(r1 * t) + c2 * std::exp(r2 * t));
    } else {
        // Critically damped (zeta == 1)
        f32 envelope = std::exp(-omega_n * t);
        return to - (to - from) * envelope * (1.0f + omega_n * t);
    }
}

inline f32 spring(Frame frame, FrameRate fps, f32 from, f32 to, const SpringConfig& config = {}) {
    f32 t = static_cast<f32>(frame) / static_cast<f32>(fps.fps());
    return spring(t, from, to, config);
}

inline f32 spring(const FrameContext& ctx, f32 from, f32 to, const SpringConfig& config = {}) {
    return spring(ctx.frame, ctx.frame_rate, from, to, config);
}

inline f32 spring(const SequenceContext& ctx, f32 from, f32 to, const SpringConfig& config = {}) {
    return spring(ctx.frame, ctx.parent.frame_rate, from, to, config);
}

} // namespace chronon3d
