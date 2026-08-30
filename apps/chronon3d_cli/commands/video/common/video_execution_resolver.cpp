#include "video_execution_resolver.hpp"

namespace chronon3d::cli {

VideoExecutionDecision resolve_video_execution(
    const VideoExecutionRequest& request) noexcept {
    if (request.has_gop_source && request.gop_copy_only) {
        return {VideoExecutionPath::BitstreamCopy, "explicit_gop_copy", true};
    }
    const bool native_nvenc = request.encoder_backend == "native" &&
                              request.hardware_encoder == "nvenc";
    if (request.hot_path == GpuHotPathMode::RequireDirectYuv) {
        if (!native_nvenc) {
            return {VideoExecutionPath::DirectYuv,
                    "direct_yuv_requires_native_nvenc", false};
        }
        return {VideoExecutionPath::DirectYuv, "required_direct_yuv", true};
    }
    return {VideoExecutionPath::FullGraph,
            native_nvenc ? "native_nvenc_full_graph" : "full_graph_default", true};
}

} // namespace chronon3d::cli
