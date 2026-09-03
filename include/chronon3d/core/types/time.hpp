#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace chronon3d {
using TimeSeconds = f64;

/// Exact rational used for media time-bases. A denominator of zero is invalid;
/// normalization is performed by normalize_rational() at conversion boundaries.
struct Rational {
    i64 numerator{1};
    i64 denominator{1};

    constexpr bool operator==(const Rational&) const = default;
};

[[nodiscard]] inline Rational normalize_rational(Rational value) {
    if (value.denominator == 0) {
        throw std::invalid_argument("rational denominator must be non-zero");
    }
    if (value.denominator < 0) {
        value.numerator = -value.numerator;
        value.denominator = -value.denominator;
    }
    const i64 divisor = std::gcd(value.numerator, value.denominator);
    if (divisor != 0) {
        value.numerator /= divisor;
        value.denominator /= divisor;
    }
    return value;
}

/// Exact presentation-time coordinate. `value` is expressed in `time_base`
/// units (for example PTS=90000 at time_base=1/90000 is exactly one second).
/// DTS deliberately does not belong to this render-domain type.
struct RationalTime {
    i64 value{0};
    Rational time_base{1, 1};

    constexpr bool operator==(const RationalTime&) const = default;

    /// Raw tick count expressed in `time_base` units. Named accessor (the
    /// `Frame::integral()` precedent): the `value` member stays an
    /// implementation detail and the Frame::value convention gate only
    /// allows accessor reads outside this header.
    [[nodiscard]] constexpr i64 ticks() const noexcept { return value; }

    [[nodiscard]] TimeSeconds seconds() const {
        const Rational base = normalize_rational(time_base);
        return static_cast<TimeSeconds>(value) *
               static_cast<TimeSeconds>(base.numerator) /
               static_cast<TimeSeconds>(base.denominator);
    }
};

/// Convert an exact media timestamp to an integer Chronon timeline. The call
/// succeeds only when the target tick rate can represent the timestamp exactly;
/// callers that choose a timeline rate are therefore forced to make rounding a
/// deliberate boundary decision instead of accumulating floating-point seconds.
[[nodiscard]] inline i64 rational_time_to_ticks_exact(
    RationalTime time,
    i64 ticks_per_second) {
    if (ticks_per_second <= 0) {
        throw std::invalid_argument("ticks_per_second must be positive");
    }
    const Rational base = normalize_rational(time.time_base);
    const __int128 scaled = static_cast<__int128>(time.value) *
                            static_cast<__int128>(base.numerator) *
                            static_cast<__int128>(ticks_per_second);
    const __int128 denominator = static_cast<__int128>(base.denominator);
    if (scaled % denominator != 0) {
        throw std::invalid_argument("rational time does not map exactly to target timeline");
    }
    const __int128 result = scaled / denominator;
    if (result < static_cast<__int128>(std::numeric_limits<i64>::min()) ||
        result > static_cast<__int128>(std::numeric_limits<i64>::max())) {
        throw std::overflow_error("rational time conversion overflow");
    }
    return static_cast<i64>(result);
}

/// Frame-local exact time carried by the render graph. Presentation time keeps
/// the original media time-base while timeline_tick is the compiled integer
/// coordinate selected by the caller. Decode timestamps remain outside this
/// contract in demux/decoder/encoder/mux code.
struct FrameTimeContext {
    Frame output_frame{0};
    RationalTime presentation_time{};
    RationalTime duration{};
    i64 timeline_tick{0};
    bool discontinuity{false};

    constexpr bool operator==(const FrameTimeContext&) const = default;
};

/// Explicit temporal dependency contract for passes/effects. A zero-valued
/// contract means frame-local/pure execution. Temporal implementations must
/// declare the history/future window they need instead of hiding it in state.
struct TemporalRequirements {
    i32 history_frames{0};
    i32 future_frames{0};
    RationalTime history_duration{};
    RationalTime future_duration{};

    [[nodiscard]] bool valid() const noexcept {
        return history_frames >= 0 && future_frames >= 0;
    }
    [[nodiscard]] bool is_temporal() const noexcept {
        return history_frames != 0 || future_frames != 0 ||
               history_duration.ticks() != 0 || future_duration.ticks() != 0;
    }

    constexpr bool operator==(const TemporalRequirements&) const = default;
};

enum class FrameRounding {
    Nearest, ///< Round to the nearest frame (half values away from zero).
    Floor,   ///< Round down to the previous frame.
    Ceiling, ///< Round up to the next frame.
};

enum class MinimumFrameDuration {
    AllowEmpty,
    AtLeastOneFrame,
};

struct FrameRate {
    i32 numerator{30};
    i32 denominator{1};

    [[nodiscard]] constexpr TimeSeconds to_seconds(Frame frame) const {
        return static_cast<TimeSeconds>(frame) * denominator / numerator;
    }

    [[nodiscard]] constexpr f64 fps() const {
        return static_cast<f64>(numerator) / denominator;
    }

    /// Exact time-base of one output frame. No fixed global clock is assumed.
    [[nodiscard]] constexpr Rational frame_time_base() const noexcept {
        return Rational{denominator, numerator};
    }

    [[nodiscard]] constexpr RationalTime presentation_time(Frame frame) const noexcept {
        return RationalTime{frame.integral(), frame_time_base()};
    }

    [[nodiscard]] constexpr RationalTime frame_duration() const noexcept {
        return RationalTime{1, frame_time_base()};
    }

    // ── Named component accessors (preferred for readability) ────────
    [[nodiscard]] constexpr i32 num() const noexcept { return numerator; }
    [[nodiscard]] constexpr i32 den() const noexcept { return denominator; }

    constexpr bool operator==(const FrameRate&) const = default;
};

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

[[nodiscard]] inline TimeRange resolve_frame_range(
    TimeSeconds start_seconds,
    TimeSeconds end_seconds,
    FrameRate rate,
    MinimumFrameDuration minimum = MinimumFrameDuration::AllowEmpty,
    FrameRounding rounding = FrameRounding::Nearest) {
    const Frame start = seconds_to_frame(start_seconds, rate, rounding);
    Frame end = seconds_to_frame(end_seconds, rate, rounding);
    if (end < start) {
        end = start;
    }
    if (minimum == MinimumFrameDuration::AtLeastOneFrame && end <= start) {
        end = start + Frame{1};
    }
    return TimeRange{.start = start, .end = end};
}

} // namespace chronon3d
