#include <chronon3d/backends/ffmpeg/ffmpeg_video_backend.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_frame_extractor.hpp>

namespace chronon3d::video {

std::unique_ptr<VideoFrameDecoder> make_ffmpeg_frame_decoder(
    std::filesystem::path temp_dir
) {
    return std::make_unique<FfmpegFrameExtractor>(std::move(temp_dir));
}

} // namespace chronon3d::video
