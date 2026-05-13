#pragma once

#include <chronon3d/video/video_decoder.hpp>
#include <chronon3d/video/video_frame_cache.hpp>
#include <filesystem>

namespace chronon3d::video {

class FfmpegFrameExtractor final : public VideoDecoder {
public:
    explicit FfmpegFrameExtractor(std::filesystem::path temp_dir = "output/video_cache");

    std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame source_frame,
        i32 width,
        i32 height,
        f32 fps
    ) override;

    void clear_memory_cache();

private:
    std::filesystem::path m_temp_dir;
    VideoFrameCache m_cache;

    std::filesystem::path frame_path_for(
        const std::string& path,
        Frame frame,
        i32 width,
        i32 height
    ) const;
};

} // namespace chronon3d::video
