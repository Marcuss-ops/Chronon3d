#pragma once

#include <string>
#include <vector>

namespace chronon3d::video {

struct VideoEncodeOptions {
    std::string ffmpeg_path{"ffmpeg"};
    std::string codec{"libx264"};
    std::string pix_fmt{"yuv420p"};
    std::string preset{"medium"};
    int crf{18};
    int fps{30};
    std::string extra_args; // e.g., "-movflags +faststart"
};

class FfmpegEncoder {
public:
    static bool is_available(const std::string& ffmpeg_path = "ffmpeg");

    static int encode(const std::string& frame_pattern, 
                      const std::string& output_path,
                      const VideoEncodeOptions& options);

    static std::string build_command(const std::string& frame_pattern,
                                     const std::string& output_path,
                                     const VideoEncodeOptions& options);
};

} // namespace chronon3d::video
