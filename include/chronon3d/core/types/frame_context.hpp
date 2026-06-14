#pragma once

#include <chronon3d/core/types/time.hpp>
#include <algorithm>
#include <memory_resource>

namespace chronon3d {

struct FrameContext {
    Frame frame{0};
    Frame local_frame{0};    // alias/local offset for sequences
    f32   frame_time{0.0f};  // fractional frame for motion blur subsampling (0.0 = integral frame)
    Frame duration{0};
    FrameRate frame_rate{30, 1};
    i32 width{1920};
    i32 height{1080};
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};

    [[nodiscard]] double fps() const { return frame_rate.fps(); }

    // Effective time: integral frame + fractional offset.
    [[nodiscard]] double effective_frame() const {
        return static_cast<double>(frame) + static_cast<double>(frame_time);
    }

    [[nodiscard]] TimeSeconds seconds() const {
        return effective_frame()
             * static_cast<double>(frame_rate.denominator)
             / static_cast<double>(frame_rate.numerator);
    }

    [[nodiscard]] double progress() const {
        if (duration <= 0) return 0.0;
        return std::clamp(
            effective_frame() / static_cast<double>(duration),
            0.0,
            1.0
        );
    }

    [[nodiscard]] bool is_first_frame() const { return frame == 0; }
    [[nodiscard]] bool is_last_frame() const { return duration > 0 && frame >= duration - 1; }
};

} // namespace chronon3d
