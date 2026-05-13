#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
#include <memory>
#include <string>

namespace chronon3d::video {

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    virtual std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame source_frame,
        i32 width,
        i32 height,
        f32 fps
    ) = 0;
};

} // namespace chronon3d::video
