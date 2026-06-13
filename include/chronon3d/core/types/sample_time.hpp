#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>

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
// The type is trivially copyable and cheap to pass by value.

struct SampleTime {
    double frame{0.0};    // fractional frame (e.g. 12.375)
    double seconds{0.0};  // fractional seconds (frame / fps)
    double fps{30.0};     // frame rate used for conversions

    // ── factories ────────────────────────────────────────────────────────────
    static SampleTime from_frame(double frame, double fps = 30.0) {
        return { frame, frame / fps, fps };
    }

    static SampleTime from_seconds(double seconds, double fps = 30.0) {
        return { seconds * fps, seconds, fps };
    }

    // Convenience: from integer Frame (backward compatibility).
    static SampleTime from_frame_int(Frame frame, double fps = 30.0) {
        const double fd = static_cast<double>(frame);
        return { fd, fd / fps, fps };
    }

    // Equality (C++20 default)
    bool operator==(const SampleTime&) const = default;
};


// ── SampleTimeKey — deterministic integer key for cache hashing ─────────────
//
// Floating-point `SampleTime` is not safe as a hash key (NaN, subnormals,
// non-deterministic rounding).  `SampleTimeKey` quantises time to a fixed
// tick resolution so that two `SampleTime` values that represent the same
// conceptual instant always map to the same key.
//
// Tick resolution: 1 tick = 1/65536 of a frame (~0.5 µs at 30 fps).
// This is fine enough for any practical sub-frame sampling while keeping
// the key an `int64_t`.

struct SampleTimeKey {
    int64_t ticks{0};

    static constexpr int64_t kTicksPerFrame = 65536;

    static SampleTimeKey from_sample_time(const SampleTime& t) {
        return { std::llround(t.frame * static_cast<double>(kTicksPerFrame)) };
    }

    static SampleTimeKey from_frame_int(Frame frame) {
        return { static_cast<int64_t>(frame) * kTicksPerFrame };
    }

    // Comparison / hashing support (use SampleTimeKeyHash for std::unordered_map)
    bool operator==(const SampleTimeKey&) const = default;
    bool operator<(const SampleTimeKey& other) const { return ticks < other.ticks; }

    // Conversion back (approximate, for debug/logging only)
    SampleTime to_sample_time(double fps = 30.0) const {
        const double f = static_cast<double>(ticks) / static_cast<double>(kTicksPerFrame);
        return { f, f / fps, fps };
    }
};

} // namespace chronon3d

// ── SampleTimeKeyHash — hashing functor for std::unordered_map ────────────────
namespace chronon3d {

struct SampleTimeKeyHash {
    size_t operator()(const SampleTimeKey& k) const noexcept {
        return std::hash<int64_t>{}(k.ticks);
    }
};

} // namespace chronon3d
