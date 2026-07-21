#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cmath>

namespace chronon3d {
using TimeSeconds = f64;

enum class FrameRounding {
    Nearest, ///< Round to the nearest frame (half values away from zero).
    Floor,   ///< Round down to the previous frame.
    Ceiling, ///< Round up to the next frame.
};

struct FrameRate {
    i32 numerator{30};
    i32 denominator{1};

    [[nodiscard]] constexpr TimeSeconds to_seconds(Frame frame) const {
        return static_cast<TimeSeconds>(frame) * denominator / numerator;
    }

    [[nodiscard]] constexpr Frame to_frame(TimeSeconds seconds) const {
        return static_cast<Frame>(seconds * numerator / denominator);
    }

    [[nodiscard]] constexpr f64 fps() const {
        return static_cast<f64>(numerator) / denominator;
    }

    // ── Named component accessors (preferred for readability) ────────
    // Short aliases for `numerator` / `denominator` — match common
    // math notation `fps = num/den` and authoring DSL chains
    // (`.frame_rate(FrameRate{30, 1}).num` reads better than
    // `.numerator` at call sites).
    [[nodiscard]] constexpr i32 num() const noexcept { return numerator; }
    [[nodiscard]] constexpr i32 den() const noexcept { return denominator; }

    // Equality — needed for SampleTime::operator== (defaulted) to compile.
    constexpr bool operator==(const FrameRate&) const = default;
};

/// Convert a seconds value to a frame number using an explicit rounding policy.
///
/// This is the canonical place for seconds→frame conversion so that every
/// consumer (subtitles, animations, audio scheduling) uses the same semantics.
/// The default rounding is `Nearest`; callers that need [start, end)
/// boundaries typically use `Nearest` for both endpoints.
[[nodiscard]] inline Frame seconds_to_frame(
    TimeSeconds seconds,
    FrameRate rate,
    FrameRounding rounding = FrameRounding::Nearest
) {
    const double raw = seconds * static_cast<double>(rate.numerator)
                       / static_cast<double>(rate.denominator);
    switch (rounding) {
        case FrameRounding::Floor:
            return static_cast<Frame>(std::floor(raw));
        case FrameRounding::Ceiling:
            return static_cast<Frame>(std::ceil(raw));
        case FrameRounding::Nearest:
        default:
            return static_cast<Frame>(std::lround(raw));
    }
}

struct TimeRange {
    Frame start{0};
    Frame end{0};

    [[nodiscard]] constexpr Frame duration() const { return end - start; }

    [[nodiscard]] constexpr bool contains(Frame frame) const {
        return frame >= start && frame < end;
    }

    [[nodiscard]] constexpr f32 normalized(Frame frame) const {
        if (duration() == 0) return 0.0f;
        return static_cast<f32>(frame - start) / static_cast<f32>(duration());
    }
};

} // namespace chronon3d
