#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>

#include <cmath>
#include <cstdint>
#include <functional>

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

    // ── legacy factories (deprecated — keep for transition) ──────────────────
    // Preserve fractional FPS (e.g. 29.97 NTSC) by scaling to milliseconds
    // instead of truncating via static_cast<i32>(fps).
    static SampleTime from_frame(double frame, double fps) {
        const auto fps_ms = static_cast<i32>(std::llround(fps * 1000.0));
        return { frame, FrameRate{fps_ms, 1000} };
    }

    static SampleTime from_seconds(double seconds, double fps) {
        const auto fps_ms = static_cast<i32>(std::llround(fps * 1000.0));
        return { seconds * fps, FrameRate{fps_ms, 1000} };
    }

    static SampleTime from_frame_int(Frame frame, double fps) {
        const auto fps_ms = static_cast<i32>(std::llround(fps * 1000.0));
        return { static_cast<double>(frame), FrameRate{fps_ms, 1000} };
    }

    /// Legacy: from integer Frame with implicit 30 FPS (deprecated).
    static SampleTime from_frame_int(Frame frame) {
        return { static_cast<double>(frame), FrameRate{30, 1} };
    }

    // Equality (C++20 default)
    bool operator==(const SampleTime&) const = default;
};


// ── TemporalSampleKey — deterministic cache key with version ──────────────
//
// Replaces `SampleTimeKey` (which used absolute ticks).  `TemporalSampleKey`
// separates the integral frame, sub-frame tick, and a content version so that:
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

// ── SampleTimeKey (deprecated — replaced by TemporalSampleKey) ──────────────
//
// Kept for backward compatibility during migration.  New code should use
// `TemporalSampleKey` and `make_temporal_key()` instead.

struct SampleTimeKey {
    int64_t ticks{0};

    static constexpr int64_t kTicksPerFrame = 65536;

    [[deprecated("Use make_temporal_key() instead")]]
    static SampleTimeKey from_sample_time(const SampleTime& t) {
        const double base = std::floor(t.frame);
        const double fraction = t.frame - base;
        auto tick = static_cast<int64_t>(
            std::llround(fraction * static_cast<double>(kTicksPerFrame)));
        int64_t frame_part = static_cast<int64_t>(base);
        if (tick >= kTicksPerFrame) {
            ++frame_part;
            tick = 0;
        } else if (tick < 0) {
            --frame_part;
            tick += kTicksPerFrame;
        }
        return { frame_part * kTicksPerFrame + tick };
    }

    [[deprecated("Use TemporalSampleKey instead")]]
    static SampleTimeKey from_frame_int(Frame frame) {
        return { static_cast<int64_t>(frame) * kTicksPerFrame };
    }

    bool operator==(const SampleTimeKey&) const = default;
    bool operator<(const SampleTimeKey& other) const { return ticks < other.ticks; }

    SampleTime to_sample_time(FrameRate rate = FrameRate{30, 1}) const {
        const double f = static_cast<double>(ticks) / static_cast<double>(kTicksPerFrame);
        return SampleTime::from_frame(f, rate);
    }
};

} // namespace chronon3d

// ── TemporalSampleKeyHash — hashing functor (forward-looking) ─────────────
//
// Currently FrameCacheKey / NodeCacheKey use their own XXH3-based digest()
// method.  This hash is provided for future std::unordered_map<TemporalSampleKey>
// usage (e.g. per-node temporal key maps).

namespace chronon3d {

struct TemporalSampleKeyHash {
    size_t operator()(const TemporalSampleKey& k) const noexcept {
        // Combine frame, subframe_tick, and version with a simple hash.
        size_t h = std::hash<Frame>{}(k.frame);
        h ^= std::hash<u32>{}(k.subframe_tick) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<EvaluationVersion>{}(k.version) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace chronon3d

// ── SampleTimeKeyHash — hashing functor for deprecated SampleTimeKey ────────
namespace chronon3d {

struct SampleTimeKeyHash {
    size_t operator()(const SampleTimeKey& k) const noexcept {
        return std::hash<int64_t>{}(k.ticks);
    }
};

} // namespace chronon3d
