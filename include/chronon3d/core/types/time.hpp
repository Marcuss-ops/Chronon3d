#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>

namespace chronon3d {
using TimeSeconds = f64;

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
