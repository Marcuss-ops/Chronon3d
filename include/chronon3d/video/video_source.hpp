#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <string>

namespace chronon3d::video {

enum class VideoLoopMode {
    None,
    HoldLastFrame,
    Loop,
    PingPong
};

struct VideoSource {
    std::string path;

    Frame source_start{0};
    Frame duration{-1};

    f32 source_fps{30.0f};
    f32 speed{1.0f};

    VideoLoopMode loop_mode{VideoLoopMode::HoldLastFrame};

    bool cache_frames{true};
};

[[nodiscard]] Frame map_video_frame(Frame local_layer_frame, const VideoSource& source);

} // namespace chronon3d::video
