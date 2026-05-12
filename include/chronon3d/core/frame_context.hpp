#pragma once

#include <chronon3d/core/time.hpp>
#include <memory_resource>

namespace chronon3d {

struct FrameContext {
    Frame frame{0};
    f32   frame_time{0.0f};  // fractional frame for motion blur subsampling (0.0 = integral frame)
    Frame duration{0};
    FrameRate frame_rate{30, 1};
    i32 width{1920};
    i32 height{1080};
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};

    // Effective time: integral frame + fractional offset.
    [[nodiscard]] f32 effective_frame() const {
        return static_cast<f32>(frame) + frame_time;
    }

    [[nodiscard]] TimeSeconds seconds() const {
        return frame_rate.to_seconds(frame);
    }

    [[nodiscard]] f32 progress() const {
        if (duration <= 0) return 0.0f;
        return static_cast<f32>(frame) / static_cast<f32>(duration);
    }

    [[nodiscard]] bool is_first_frame() const { return frame == 0; }
    [[nodiscard]] bool is_last_frame() const { return duration > 0 && frame >= duration - 1; }
};

} // namespace chronon3d
