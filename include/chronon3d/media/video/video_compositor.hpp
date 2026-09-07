#pragma once

#include <chronon3d/media/video/video_composite_request.hpp>

namespace chronon3d::media::video {

class VideoCompositor {
public:
    virtual ~VideoCompositor() = default;
    virtual CompositeResult composite(const VideoCompositeRequest&) = 0;
};

} // namespace chronon3d::media::video
