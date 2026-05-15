#include <chronon3d/backends/video/video_source.hpp>
#include <algorithm>

namespace chronon3d::video {

Frame map_video_frame(Frame local_layer_frame, const VideoSource& source) {
    if (local_layer_frame < 0) {
        return source.source_start;
    }

    Frame mapped = source.source_start + static_cast<Frame>(
        static_cast<f32>(local_layer_frame) * source.speed
    );

    if (source.duration <= 0) {
        return mapped;
    }

    const Frame start = source.source_start;
    const Frame end = source.source_start + source.duration;

    if (mapped < end) {
        return mapped;
    }

    switch (source.loop_mode) {
    case VideoLoopMode::None:
    case VideoLoopMode::HoldLastFrame:
        return end - 1;

    case VideoLoopMode::Loop:
        return start + ((mapped - start) % source.duration);

    case VideoLoopMode::PingPong: {
        const Frame cycle = source.duration * 2;
        Frame pos = (mapped - start) % cycle;
        if (pos >= source.duration) {
            pos = cycle - pos - 1;
        }
        return start + pos;
    }
    }

    return mapped;
}

} // namespace chronon3d::video
