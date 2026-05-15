#pragma once

#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::video {

struct VideoExportOptions {
    Frame start{0};
    Frame end{0};
    VideoEncodeOptions encode{};
};

bool render_to_mp4(SoftwareRenderer& renderer,
                   const Composition& comp,
                   const std::string& output_path,
                   const VideoExportOptions& options);

} // namespace chronon3d::video
