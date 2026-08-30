#pragma once

#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <string_view>

namespace chronon3d::media {

enum class VideoExecutionPath : unsigned char {
    BitstreamCopy,
    DirectYuv,
    FullGraph,
};

struct VideoExecutionRequest {
    std::string_view encoder_backend;
    std::string_view hardware_encoder;
    std::string_view codec;
    GpuHotPathMode hot_path{GpuHotPathMode::Auto};
    bool has_gop_source{false};
    bool gop_copy_only{false};
};

struct VideoExecutionDecision {
    VideoExecutionPath path{VideoExecutionPath::FullGraph};
    std::string_view reason{"full_graph_default"};
    bool valid{true};
};

[[nodiscard]] VideoExecutionDecision resolve_video_execution(
    const VideoExecutionRequest& request) noexcept;

} // namespace chronon3d::media
