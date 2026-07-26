#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace chronon3d {

// ── SpringConfig — canonical damped harmonic oscillator parameters ───────
// Single source of truth (TICKET-ANIM-SPRING-UNIFY). Field order is canonical:
// mass, stiffness, damping, initial_velocity. Do NOT introduce a second
// SpringConfig under the chronon3d:: namespace. Single math function:
// sample_spring(TimeSeconds, f32 from, f32 to, SpringConfig).
struct SpringConfig {
    f32 mass{1.0f};                  // inertial mass (kg). Non-zero.
    f32 stiffness{100.0f};           // spring constant k. Positive.
    f32 damping{15.0f};              // damping coefficient c. Positive.
    f32 initial_velocity{0.0f};      // x'(0) — initial velocity at t=0 (units/s).
};

struct SpringSample {
    f32 value{0.0f};
    f32 velocity{0.0f};

    // Source compatibility for pre-SpringSample callers. New code should
    // read `.value` explicitly or use the value-only `spring()` adapter.
    constexpr operator f32() const noexcept { return value; }
};

inline constexpr f32 kCriticalEpsilon = 1e-4f;

[[nodiscard]] inline bool is_valid_spring_config(
    const SpringConfig& config) noexcept {
    return std::isfinite(config.mass) && config.mass > 0.0f
        && std::isfinite(config.stiffness) && config.stiffness > 0.0f
        && std::isfinite(config.damping) && config.damping >= 0.0f
        && std::isfinite(config.initial_velocity);
}

inline void validate_spring_config(const SpringConfig& config) {
    if (!is_valid_spring_config(config)) {
        throw std::invalid_argument(
            "SpringConfig requires finite mass/stiffness/velocity, "
            "mass > 0, stiffness > 0 and damping >= 0");
    }
}

namespace Spring {
    // Presets: {mass, stiffness, damping, initial_velocity}. initial_velocity=0
    // yields a trajectory starting at rest from the `from` value.
    inline constexpr SpringConfig Gentle{1.0f, 120.0f, 14.0f, 0.0f};
    inline constexpr SpringConfig Snappy{1.0f, 200.0f, 18.0f, 0.0f};
    inline constexpr SpringConfig Bouncy{1.0f, 300.0f, 12.0f, 0.0f};
    inline constexpr SpringConfig Heavy {1.0f,  80.0f, 20.0f, 0.0f};
}

/**
 * Deterministic, closed-form spring animation.
 *
 * Closed-form solution of the damped harmonic oscillator:
 *   m·x'' + c·x' + k·(x - to) = 0
 * with x(0)=from, x'(0)=v0 (config.initial_velocity).
 *
 * Three regimes by damping ratio ζ = c / (2·sqrt(k·m)):
 *   ζ < 1 — underdamped   (oscillates)
 *   ζ > 1 — overdamped    (no oscillation)
 *   ζ = 1 — critically damped (fastest non-oscillating)
 *
 * Determinism note: closed-form pure functions, no accumulator state.
 * Cross-platform bit-identity relies on std::exp / std::sin / std::cos
 * which may differ across libcs; tests assert std::isfinite and
 * tolerance-based equality only.
 */
inline SpringSample sample_spring(TimeSeconds time,
                                  f32 from,
                                  f32 to,
                                  const SpringConfig& config = {}) {
    validate_spring_config(config);
    const f32 t = static_cast<f32>(time);
    if (t <= 0.0f) return {from, config.initial_velocity};
    if (from == to && config.initial_velocity == 0.0f) return {to, 0.0f};

    const f32 omega_n = std::sqrt(config.stiffness / config.mass);
    const f32 zeta    = config.damping
                      / (2.0f * std::sqrt(config.stiffness * config.mass));
    const f32 y0 = from - to;          // initial displacement relative to target
    const f32 v0 = config.initial_velocity;

    if (zeta < 1.0f) {
        // Underdamped: y(t) = e^(-ζωn·t) · (y0·cos(ωd·t) + ((v0+ζωn·y0)/ωd)·sin(ωd·t))
        const f32 omega_d  = omega_n * std::sqrt(1.0f - zeta * zeta);
        const f32 c1 = y0;
        const f32 c2 = (v0 + zeta * omega_n * y0) / omega_d;
        const f32 envelope = std::exp(-zeta * omega_n * t);
        const f32 cos_part = std::cos(omega_d * t);
        const f32 sin_part = std::sin(omega_d * t);
        const f32 displacement = envelope * (c1 * cos_part + c2 * sin_part);
        const f32 sample_velocity = envelope * (
            (-zeta * omega_n * c1 + omega_d * c2) * cos_part
            + (-zeta * omega_n * c2 - omega_d * c1) * sin_part);
        return {to + displacement, sample_velocity};
    } else if (std::abs(zeta - 1.0f) > kCriticalEpsilon) {
        // Overdamped: y(t) = C1·e^(r1·t) + C2·e^(r2·t)
        // with r1 = -ζωn+ωr, r2 = -ζωn-ωr (ωr = ωn·sqrt(ζ²-1))
        // Initial conditions yield:
        //   C2 = (v0 - r1·y0) / (r2 - r1)
        //   C1 = y0 - C2
        const f32 omega_r = omega_n * std::sqrt(zeta * zeta - 1.0f);
        const f32 r1 = -zeta * omega_n + omega_r;
        const f32 r2 = -zeta * omega_n - omega_r;
        const f32 c2 = (v0 - r1 * y0) / (r2 - r1);
        const f32 c1 = y0 - c2;
        // Keep the value expression in the historical grouping so adding
        // velocity sampling does not perturb the value-only render path.
        const f32 value = to + (c1 * std::exp(r1 * t) +
                                c2 * std::exp(r2 * t));
        const f32 e1 = std::exp(r1 * t);
        const f32 e2 = std::exp(r2 * t);
        return {value, c1 * r1 * e1 + c2 * r2 * e2};
    } else {
        // Near-critical values use the stable critically damped solution.
        const f32 c2 = v0 + omega_n * y0;
        const f32 envelope = std::exp(-omega_n * t);
        const f32 displacement = envelope * (y0 + c2 * t);
        const f32 sample_velocity = envelope * (c2 - omega_n * (y0 + c2 * t));
        return {to + displacement, sample_velocity};
    }
}

/// Value-only adapter used by render/property authoring code.
inline f32 spring(TimeSeconds time,
                  f32 from,
                  f32 to,
                  const SpringConfig& config = {}) {
    return sample_spring(time, from, to, config).value;
}

// ── Frame-aware wrappers around sample_spring ───────────────────────────
// These overloads preserve the historical public API surface; they all
// delegate to the canonical sample_spring(TimeSeconds, ...).

inline f32 spring(Frame frame,
                  FrameRate fps,
                  f32 from,
                  f32 to,
                  const SpringConfig& config = {}) {
    return spring(fps.to_seconds(frame), from, to, config);
}

inline f32 spring(const FrameContext& ctx,
                  f32 from,
                  f32 to,
                  const SpringConfig& config = {}) {
    return spring(ctx.frame_rate().to_seconds(ctx.frame()),
                  from, to, config);
}

} // namespace chronon3d
