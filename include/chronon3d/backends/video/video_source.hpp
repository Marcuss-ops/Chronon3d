#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/vec2.hpp>

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

[[nodiscard]] Frame map_video_frame(Frame local_frame, const VideoSource& source);

} // namespace chronon3d::video
