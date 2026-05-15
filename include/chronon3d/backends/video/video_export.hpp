#pragma once

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/video/video_encoder.hpp>

namespace chronon3d::video {

struct VideoExportOptions {
    Frame start{0};
    Frame end{0};
    VideoEncodeOptions encode{};
};

bool render_to_video(SoftwareRenderer& renderer,
                     const Composition& comp,
                     const std::string& output_path,
                     const VideoExportOptions& options,
                     IEncoder& encoder);

} // namespace chronon3d::video
