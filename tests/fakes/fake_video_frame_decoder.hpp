#pragma once

#include <chronon3d/backends/video/video_frame_decoder.hpp>

namespace chronon3d::test {

class FakeVideoFrameDecoder final : public video::VideoFrameDecoder {
public:
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string&,
        Frame,
        i32 width,
        i32 height,
        f32
    ) override {
        auto fb = std::make_shared<Framebuffer>(width, height);
        fb->clear(Color::transparent());
        return fb;
    }
};

} // namespace chronon3d::test
