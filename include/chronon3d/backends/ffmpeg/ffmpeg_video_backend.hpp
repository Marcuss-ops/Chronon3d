#pragma once

#include <chronon3d/backends/video/video_frame_decoder.hpp>

#include <filesystem>
#include <memory>

namespace chronon3d::video {

[[nodiscard]] std::unique_ptr<VideoFrameDecoder> make_ffmpeg_frame_decoder(
    std::filesystem::path temp_dir = "output/video_cache"
);

} // namespace chronon3d::video
