#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <cstdint>

namespace chronon3d {

// ── Deterministic value noise ────────────────────────────────────────────────
// Simple hash-based noise for deterministic, seedable randomness.
// Returns values in [-1, 1]. Used as building block for wiggle().

namespace detail {

[[nodiscard]] inline f32 hash_noise(i32 n, u32 seed = 0) {
    // Based on Squirrel3 (GDC 2017 talk by Gabor Szauer)
    u32 x = static_cast<u32>(n) + seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    // Map to [-1, 1]
    return static_cast<f32>(x % 10000u) / 5000.0f - 1.0f;
}

[[nodiscard]] inline f32 smooth_noise(f32 t, u32 seed = 0) {
    const i32 i = static_cast<i32>(std::floor(t));
    const f32 frac = t - static_cast<f32>(i);

    // Smoothstep interpolation
    const f32 u = frac * frac * (3.0f - 2.0f * frac);

    const f32 a = hash_noise(i, seed);
    const f32 b = hash_noise(i + 1, seed);
    return a + (b - a) * u;
}

} // namespace detail

// ── wiggle() — After Effects-style wiggle expression ─────────────────────────
//
// Produces organic, deterministic noise-based displacement.
// Equivalent to AE's wiggle(freq, amp) expression.
//
// Usage:
//   f32 x = wiggle(3.0f, 15.0f, frame);         // 3 Hz, 15 units amplitude
//   f32 x = wiggle(3.0f, 15.0f, frame, 42);     // with seed
//
// Parameters:
//   freq    — oscillation frequency in Hz (or cycles per second)
//   amp     — maximum displacement amplitude
//   time    — current time (frame number or seconds)
//   seed    — random seed for deterministic variation (default 0)
//
// Returns: displacement value in [-amp, amp]

[[nodiscard]] inline f32 wiggle(f32 freq, f32 amp, f32 time, u32 seed = 0) {
    if (amp <= 0.0f || freq <= 0.0f) return 0.0f;

    // Layer 1: primary oscillation
    const f32 n1 = detail::smooth_noise(time * freq, seed);
    // Layer 2: double-frequency detail (half amplitude)
    const f32 n2 = detail::smooth_noise(time * freq * 2.0f, seed + 1000u) * 0.5f;
    // Layer 3: quadruple-frequency micro-detail (quarter amplitude)
    const f32 n3 = detail::smooth_noise(time * freq * 4.0f, seed + 2000u) * 0.25f;

    // Combine layers (total amplitude = 1 + 0.5 + 0.25 = 1.75, normalize)
    constexpr f32 kNorm = 1.0f / 1.75f;
    return (n1 + n2 + n3) * amp * kNorm;
}

// Overload accepting Frame (integer frame number)
[[nodiscard]] inline f32 wiggle(f32 freq, f32 amp, Frame frame, u32 seed = 0) {
    return wiggle(freq, amp, static_cast<f32>(frame), seed);
}

// ── wiggle3D() — 3-channel wiggle for Vec3 displacement ─────────────────────
//
// Produces independent noise on X, Y, Z channels with different seeds
// to avoid correlation between axes.
//
// Usage:
//   Vec3 offset = wiggle3D(3.0f, 15.0f, frame);
//   cam.position += wiggle3D(3.0f, Vec3{15, 10, 5}, frame);

[[nodiscard]] inline Vec3 wiggle3D(f32 freq, f32 amp, f32 time, u32 seed = 0) {
    return Vec3{
        wiggle(freq, amp, time, seed),
        wiggle(freq, amp, time, seed + 100u),
        wiggle(freq, amp, time, seed + 200u)
    };
}

[[nodiscard]] inline Vec3 wiggle3D(f32 freq, f32 amp, Frame frame, u32 seed = 0) {
    return wiggle3D(freq, amp, static_cast<f32>(frame), seed);
}

[[nodiscard]] inline Vec3 wiggle3D(f32 freq, Vec3 amp, f32 time, u32 seed = 0) {
    return Vec3{
        wiggle(freq, amp.x, time, seed),
        wiggle(freq, amp.y, time, seed + 100u),
        wiggle(freq, amp.z, time, seed + 200u)
    };
}

[[nodiscard]] inline Vec3 wiggle3D(f32 freq, Vec3 amp, Frame frame, u32 seed = 0) {
    return wiggle3D(freq, amp, static_cast<f32>(frame), seed);
}// CameraShakeConfig and ShakePreset are defined in camera_shake.hpp
// to avoid circular includes with camera_2_5d.hpp

} // namespace chronon3d
