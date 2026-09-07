#pragma once

#include <chronon3d/media/video/video_compositor.hpp>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace chronon3d::media::video {

struct VideoCompositorCapabilities { bool cuda_nv12{false}; };
struct VideoExecutionRequirements { bool require_cuda{false}; };

class VideoCompositorRegistry final {
public:
    using Factory = std::function<std::unique_ptr<VideoCompositor>()>;
    void register_factory(std::string_view name, Factory factory);
    [[nodiscard]] std::unique_ptr<VideoCompositor> create(std::string_view name) const;
private:
    struct Entry { std::string name; Factory factory; };
    std::vector<Entry> entries_;
};

} // namespace chronon3d::media::video
