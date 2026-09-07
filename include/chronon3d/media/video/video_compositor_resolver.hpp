#pragma once

#include <chronon3d/media/video/video_compositor_registry.hpp>

namespace chronon3d::media::video {

[[nodiscard]] std::unique_ptr<VideoCompositor> resolve_video_compositor(
    const VideoCompositorRegistry&, const VideoCompositorCapabilities&,
    const VideoExecutionRequirements&);

} // namespace chronon3d::media::video
