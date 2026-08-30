#include <chronon3d/media/video/video_execution_resolver.hpp>

namespace chronon3d::media {

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
    // Auto is a resolver decision, not an alias for FullGraph. The direct
    // program performs the composition-specific eligibility check after this
    // decision and fails closed when the compiled scene is unsupported.
    if (native_nvenc) {
        return {VideoExecutionPath::DirectYuv, "auto_direct_yuv_candidate", true};
    }
    return {VideoExecutionPath::FullGraph, "full_graph_default", true};
}

} // namespace chronon3d::media
