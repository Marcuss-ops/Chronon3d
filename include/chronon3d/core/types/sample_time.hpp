#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>

#include <cmath>
#include <cstdint>

namespace chronon3d {

// ── SampleTime — continuous temporal coordinate ──────────────────────────────
//
// Replaces discrete `Frame` in animation evaluation, camera rigs, and
// sub-frame motion-blur sampling.  Every consumer that previously took
// `Frame` should eventually migrate to `SampleTime` (with a backward-
// compatible `Frame` overload kept temporarily).
//
// The type stores a single canonical coordinate (frame + frame_rate) and
// derives all other representations.  This prevents accidental mixed-
// precision states (e.g. frame=30, seconds=2, fps=30).
//
// The type is trivially copyable and cheap to pass by value.

struct SampleTime {
    double frame{0.0};           // fractional frame — single source of truth
    FrameRate frame_rate{30, 1}; // frame rate for time conversions

    // ── derived accessors ────────────────────────────────────────────────────

    /// Wall-clock seconds for the current sub-frame instant.
    [[nodiscard]] double seconds() const noexcept {
        return frame * static_cast<double>(frame_rate.denominator)
                    / static_cast<double>(frame_rate.numerator);
    }

    /// Integer frame (floor).
    [[nodiscard]] Frame integral_frame() const noexcept {
        return static_cast<Frame>(std::floor(frame));
    }

    /// Sub-frame fractional part (∈ [0, 1)).
    [[nodiscard]] double fraction() const noexcept {
        return frame - std::floor(frame);
    }

    /// Frame rate as scalar fps.
    [[nodiscard]] double fps() const noexcept {
        return frame_rate.fps();
    }

    // ── factories (canonical: require FrameRate) ─────────────────────────────
    static SampleTime from_frame(double frame, FrameRate rate) noexcept {
        return { frame, rate };
    }

    static SampleTime from_seconds(double seconds, FrameRate rate) noexcept {
        return { seconds * rate.fps(), rate };
    }

    // Convenience: from integer Frame (backward compatibility).
    static SampleTime from_frame_int(Frame frame, FrameRate rate) noexcept {
        return { static_cast<double>(frame), rate };
    }

    // Equality (C++20 default)
    bool operator==(const SampleTime&) const = default;
};


// ── TemporalSampleKey — deterministic cache key with version ──────────────
//
// Separates the integral frame, sub-frame tick, and a content version so that:
//
// • Static nodes can share the same key across all motion-blur sub-samples
//   (frame=0, subframe_tick=0, version=node_version).
//
// • Animated nodes produce different keys for each sub-frame while still
//   carrying the content version for cache invalidation.
//
// • The version field is incremented whenever the object's content changes
//   (keyframes, expressions, handles, assets, etc.), so stale cache entries
//   are never reused.
//
// Tick resolution: 1 tick = 1/65536 of a frame (~0.5 µs at 30 fps).

using EvaluationVersion = u64;

struct TemporalSampleKey {
    Frame frame{0};
    u32 subframe_tick{0};
    EvaluationVersion version{0};

    static constexpr u32 kTicksPerFrame = 65536;

    bool operator==(const TemporalSampleKey&) const = default;
};

/// Build a TemporalSampleKey from a SampleTime and a content version.
///
/// Correctly handles negative frames and tick carry via floor() + llround().
[[nodiscard]] inline TemporalSampleKey make_temporal_key(
    const SampleTime& time,
    EvaluationVersion version
) {
    const double base = std::floor(time.frame);
    const double fraction = time.frame - base;
    auto tick = static_cast<i64>(
        std::llround(fraction * static_cast<double>(TemporalSampleKey::kTicksPerFrame)));
    auto frame_part = static_cast<Frame>(base);

    if (tick >= static_cast<i64>(TemporalSampleKey::kTicksPerFrame)) {
        ++frame_part;
        tick = 0;
    }

    return {
        .frame = frame_part,
        .subframe_tick = static_cast<u32>(tick),
        .version = version
    };
}

} // namespace chronon3d
