#pragma once

#include <chronon3d/core/time.hpp>
#include <memory_resource>

namespace chronon3d {

struct FrameContext {
    Frame frame{0};
    Frame duration{0};
    FrameRate frame_rate{30, 1};
    i32 width{1920};
    i32 height{1080};
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};

    [[nodiscard]] TimeSeconds seconds() const {
        return frame_rate.to_seconds(frame);
    }

    [[nodiscard]] f32 progress() const {
        if (duration <= 0) return 0.0f;
        return static_cast<f32>(frame) / static_cast<f32>(duration);
    }
};

} // namespace chronon3d
