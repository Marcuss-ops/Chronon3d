#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <string>

namespace chronon3d::video {

enum class VideoLoopMode : u8 {
    None = 0,
    Loop = 1,
    PingPong = 2,
};

struct VideoSource {
    std::string path;
    Vec2 size{0.0f, 0.0f};
    Frame source_start{0};
    Frame duration{-1};
    f32 source_fps{30.0f};
    f32 speed{1.0f};
    VideoLoopMode loop_mode{VideoLoopMode::None};
};

namespace detail {

[[nodiscard]] inline Frame safe_loop_frame(Frame frame, Frame duration) {
    if (duration <= 0) return frame;
    const Frame mod = frame % duration;
    return mod < 0 ? mod + duration : mod;
}

} // namespace detail

[[nodiscard]] inline Frame map_video_frame(Frame local_frame, const VideoSource& source) {
    if (local_frame < 0) return source.source_start;

    const f32 scaled = static_cast<f32>(local_frame) * source.speed;
    Frame mapped = source.source_start + static_cast<Frame>(scaled);

    if (source.duration > 0) {
        const Frame rel = mapped - source.source_start;
        switch (source.loop_mode) {
            case VideoLoopMode::None:
                mapped = source.source_start + std::min(rel, source.duration - 1);
                break;
            case VideoLoopMode::Loop:
                mapped = source.source_start + detail::safe_loop_frame(rel, source.duration);
                break;
            case VideoLoopMode::PingPong: {
                const Frame period = std::max<Frame>(1, source.duration * 2 - 2);
                Frame t = detail::safe_loop_frame(rel, period);
                if (t >= source.duration) t = period - t;
                mapped = source.source_start + t;
                break;
            }
        }
    }

    return mapped;
}

} // namespace chronon3d::video
