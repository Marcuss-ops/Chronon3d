#include <chronon3d/backends/video/video_source.hpp>

#include <algorithm>

namespace chronon3d::video {

namespace {

[[nodiscard]] Frame safe_loop_frame(Frame frame, Frame duration) {
    if (duration <= 0) {
        return frame;
    }
    const Frame mod = frame % duration;
    return mod < 0 ? mod + duration : mod;
}

} // namespace

Frame map_video_frame(Frame local_frame, const VideoSource& source) {
    if (local_frame < 0) {
        return source.source_start;
    }

    const f32 scaled = static_cast<f32>(local_frame) * source.speed;
    Frame mapped = source.source_start + static_cast<Frame>(scaled);

    if (source.duration > 0) {
        const Frame rel = mapped - source.source_start;
        switch (source.loop_mode) {
            case VideoLoopMode::None:
                mapped = source.source_start + std::min(rel, source.duration - 1);
                break;
            case VideoLoopMode::Loop:
                mapped = source.source_start + safe_loop_frame(rel, source.duration);
                break;
            case VideoLoopMode::PingPong: {
                const Frame period = std::max<Frame>(1, source.duration * 2 - 2);
                Frame t = safe_loop_frame(rel, period);
                if (t >= source.duration) {
                    t = period - t;
                }
                mapped = source.source_start + t;
                break;
            }
        }
    }

    return mapped;
}

} // namespace chronon3d::video
