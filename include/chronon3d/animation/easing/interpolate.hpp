#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/time.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

enum class ClampMode {
    Clamp
};

// Extrapolate — forward-ticket TICKET-EXTRAPOLATE-ENUM bundle
// Additive-only (Cat-3 safe). Original ClampMode retained for backward
// compatibility; new code should prefer InterpolateOptions below.
//   Clamp   — t outside [0,1] is clamped (current ClampMode behavior).
//   Extend  — t outside [0,1] is left raw, output continues linearly
//             beyond the declared output range. Useful for spring
//             overshoot / bounce / elastic / back presets whose eased_t
//             legitimately leaves [0,1].
//   Wrap    — t outside [0,1] is wrapped into [0,1) modulo 1, allowing
//             looping easing curves (analogous to modulo-clamped time).
enum class Extrapolate {
    Clamp,
    Extend,
    Wrap
};

// InterpolateOptions — bundles the extrapolation policies + easing curve
// for the new interpolate(..., InterpolateOptions) overload chain (added
// in commit `feat(animation): add extrapolation policies`). Defaults
// match the legacy ClampMode-only behavior, so a default-constructed
// InterpolateOptions is bit-equivalent to the current ClampMode default.
struct InterpolateOptions {
    Extrapolate left{Extrapolate::Clamp};
    Extrapolate right{Extrapolate::Clamp};
    EasingCurve easing{Easing::Linear};
};

// ── detail helpers — TICKET-EXTRAPOLATE-ENUM Fase 2 ───────────────────────
// Internal helpers in chronon3d::animation::detail. Not exported to the
// public SDK surface (detail namespace convention). Used by the canonical
// interpolate(..., InterpolateOptions) overload below.
namespace animation::detail {

/// Wrap a normalized t value into [0, 1] per the Extrapolate policy.
///   Clamp  → std::clamp(t, 0, 1)
///   Extend → t (no clamping)
///   Wrap   → std::fmod(t, 1); negative result normalized back to [0, 1)
[[nodiscard]] inline f32 wrap_unit(f32 t, Extrapolate mode) {
    switch (mode) {
        case Extrapolate::Clamp:
            return std::clamp(t, 0.0f, 1.0f);
        case Extrapolate::Extend:
            return t;
        case Extrapolate::Wrap: {
            f32 r = std::fmod(t, 1.0f);
            if (r < 0.0f) r += 1.0f;
            return r;
        }
    }
    return t; // unreachable (suppresses -Wreturn-type)
}

/// Apply asymmetric extrapolation to a normalized t value.
///   t < 0 → wrap_unit(t, left)
///   t > 1 → wrap_unit(t, right)
///   else → t (no extrapolation needed)
[[nodiscard]] inline f32 apply_extrapolation(f32 t,
                                             Extrapolate left,
                                             Extrapolate right) {
    if (t < 0.0f) return wrap_unit(t, left);
    if (t > 1.0f) return wrap_unit(t, right);
    return t;
}

} // namespace animation::detail

inline f32 map(f32 value, f32 in_min, f32 in_max, f32 out_min, f32 out_max, ClampMode clamp = ClampMode::Clamp) {
    if (in_max == in_min) return out_min;
    f32 t = (value - in_min) / (in_max - in_min);
    if (clamp == ClampMode::Clamp) {
        t = std::clamp(t, 0.0f, 1.0f);
    }
    return out_min + (out_max - out_min) * t;
}

// ── interpolate(f32, ..., InterpolateOptions) — TICKET-EXTRAPOLATE-ENUM Fase 2 ──
//
// Canonical interpolation pipeline with explicit left/right extrapolation
// policies. This is the new target API; the legacy ClampMode overloads
// below delegate to it via the ClampMode → InterpolateOptions adapter.
inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end,
    InterpolateOptions opts
) {
    if (input_end == input_start) {
        return output_start;
    }

    f32 t = (input - input_start) / (input_end - input_start);
    t = animation::detail::apply_extrapolation(t, opts.left, opts.right);
    t = opts.easing.apply(t);

    return output_start + (output_end - output_start) * t;
}

// ── Legacy ClampMode overload — preserved for backward-compat ─────────────
//
// Adapter: bridges the old (EasingCurve, ClampMode) signature to the new
// (InterpolateOptions) overload with bit-equivalent behavior for
// ClampMode::Clamp default. New code should use InterpolateOptions directly.
inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end,
    EasingCurve easing = EasingCurve{},
    ClampMode clamp = ClampMode::Clamp
) {
    // ClampMode currently has only one value (Clamp); the conditional keeps
    // the bridge forward-compatible if new ClampMode variants are added
    // (e.g. ClampMode::Repeat → Extrapolate::Wrap). Today both arms collapse
    // to Extrapolate::Clamp, preserving bit-equivalence with the pre-Fase-2
    // behavior (Cat-3 ABI-stable).
    const Extrapolate ext = (clamp == ClampMode::Clamp)
                              ? Extrapolate::Clamp
                              : Extrapolate::Clamp;
    return interpolate(input, input_start, input_end, output_start, output_end,
                       InterpolateOptions{ext, ext, easing});
}

inline f32 interpolate(
    Frame frame,
    Frame input_start,
    Frame input_end,
    f32 output_start,
    f32 output_end,
    EasingCurve easing = EasingCurve{},
    ClampMode clamp = ClampMode::Clamp
) {
    return interpolate(
        static_cast<f32>(frame),
        static_cast<f32>(input_start),
        static_cast<f32>(input_end),
        output_start,
        output_end,
        easing,
        clamp
    );
}

} // namespace chronon3d
