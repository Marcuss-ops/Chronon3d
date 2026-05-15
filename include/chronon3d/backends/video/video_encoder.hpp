#pragma once

#include <chronon3d/core/framebuffer.hpp>

#include <memory>
#include <string>

namespace chronon3d::video {

struct VideoEncodeOptions {
    std::string codec{"auto"};
    std::string preset{"medium"};
    int crf{18};
    int fps{30};
    int gop_size{12};
    bool use_hardware_accel{false};
};

class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual bool open(const std::string& output_path,
                      const VideoEncodeOptions& options,
                      int width,
                      int height) = 0;
    virtual bool push_frame(const Framebuffer& fb) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
};

using EncoderPtr = std::unique_ptr<IEncoder>;

} // namespace chronon3d::video
