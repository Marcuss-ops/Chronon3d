#pragma once

#include <chronon3d/core/types.hpp>

namespace chronon3d {

using Frame = i64;
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
