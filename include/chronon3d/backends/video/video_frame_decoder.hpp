#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <memory>
#include <string>

namespace chronon3d::video {

class VideoFrameDecoder {
public:
    virtual ~VideoFrameDecoder() = default;

    virtual std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame frame,
        i32 width,
        i32 height,
        f32 fps) = 0;
};

} // namespace chronon3d::video
