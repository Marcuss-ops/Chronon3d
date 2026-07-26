#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/sequence.hpp>
#include <cmath>
#include <algorithm>

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
inline f32 sample_spring(TimeSeconds time,
                         f32 from,
                         f32 to,
                         const SpringConfig& config = {}) {
    const f32 t = static_cast<f32>(time);
    if (t <= 0.0f) return from;
    if (from == to && config.initial_velocity == 0.0f) return to;

    const f32 omega_n = std::sqrt(config.stiffness / config.mass);
    const f32 zeta    = config.damping
                      / (2.0f * std::sqrt(config.stiffness * config.mass));
    const f32 y0 = from - to;          // initial displacement relative to target
    const f32 v0 = config.initial_velocity;

    if (zeta < 1.0f) {
        // Underdamped: y(t) = e^(-ζωn·t) · (y0·cos(ωd·t) + ((v0+ζωn·y0)/ωd)·sin(ωd·t))
        const f32 omega_d  = omega_n * std::sqrt(1.0f - zeta * zeta);
        const f32 c1       = y0;
        const f32 c2       = (v0 + zeta * omega_n * y0) / omega_d;
        const f32 envelope = std::exp(-zeta * omega_n * t);
        const f32 cos_part = std::cos(omega_d * t);
        const f32 sin_part = std::sin(omega_d * t);
        return to + envelope * (c1 * cos_part + c2 * sin_part);
    } else if (zeta > 1.0f) {
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
        return to + (c1 * std::exp(r1 * t) + c2 * std::exp(r2 * t));
    } else {
        // Critically damped (zeta == 1): y(t) = e^(-ωn·t) · (y0 + (v0+ωn·y0)·t)
        const f32 c1       = y0;
        const f32 c2       = v0 + omega_n * y0;
        const f32 envelope = std::exp(-omega_n * t);
        return to + envelope * (c1 + c2 * t);
    }
}

// ── Frame-aware wrappers around sample_spring ───────────────────────────
// These overloads preserve the historical public API surface; they all
// delegate to the canonical sample_spring(TimeSeconds, ...).

inline f32 spring(Frame frame,
                  FrameRate fps,
                  f32 from,
                  f32 to,
                  const SpringConfig& config = {}) {
    return sample_spring(fps.to_seconds(frame), from, to, config);
}

inline f32 spring(const FrameContext& ctx,
                  f32 from,
                  f32 to,
                  const SpringConfig& config = {}) {
    return sample_spring(ctx.frame_rate().to_seconds(ctx.frame()),
                         from, to, config);
}

// Marked [[deprecated]] per TICKET-ANIM-SEQUENCE-CONSOLIDATE — same
// gradual deprecation policy as the `sequence(ctx, from, duration)`
// factory (include/chronon3d/timeline/sequence.hpp:36). The canonical
// productive path is `spring(FrameContext&, ...)` (line above) or
// `spring(Frame, FrameRate, ...)` — both delegate to the canonical
// sample_spring(TimeSeconds, ...) without requiring a SequenceContext
// adapter. Forward-point: Phase 2 ticket removes this overload once
// call-sites migrate to FrameContext-aware spring().
[[deprecated("Use spring(FrameContext&, ...) / spring(Frame, FrameRate, ...) (TICKET-ANIM-SEQUENCE-CONSOLIDATE) — gradual deprecation per AGENTS.md §2×-in-one-chore")]]
inline f32 spring(const SequenceContext& ctx,
                  f32 from,
                  f32 to,
                  const SpringConfig& config = {}) {
    return sample_spring(ctx.parent.frame_rate().to_seconds(ctx.frame),
                         from, to, config);
}

} // namespace chronon3d
