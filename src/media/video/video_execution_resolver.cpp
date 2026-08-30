#include <chronon3d/media/video/video_execution_resolver.hpp>

namespace chronon3d::media {

VideoExecutionDecision resolve_video_execution(
    const VideoExecutionRequest& request) noexcept {
    // Whole-clip BitstreamCopy: explicit request to copy every packet.
    // The caller (video_job_execute.cpp) routes directly to
    // copy_gop_source() without touching NVDEC/CUDA/NVENC.
    if (request.has_gop_source && request.gop_copy_only) {
        return {VideoExecutionPath::BitstreamCopy, "explicit_gop_copy", true,
                GopExecutionPlan{.all_copy = true, .safe_to_splice = true}};
    }

    // Smart GOP hybrid: there is a compressed source and the caller allows
    // a hybrid plan. The resolver selects SmartGopCopy and produces a
    // GopExecutionPlan describing the copy/reencode split. The actual
    // per-GOP analysis (inspect_gop_source) happens at execution time;
    // the resolver only decides that a hybrid plan IS the right path.
    //
    // This requires native NVENC: the re-encode GOPs go through DirectYUV.
    const bool native_nvenc = request.encoder_backend == "native" &&
                              request.hardware_encoder == "nvenc";
    if (request.has_gop_source && request.allow_hybrid_gop && native_nvenc) {
        // The plan details are filled by inspect_gop_source at execution
        // time; the resolver declares the intent and the safe-to-splice
        // gate is deferred to the per-GOP BitstreamCompatibility check.
        // If the splice is unsafe the execution layer downgrades to
        // DirectYuv for the entire clip.
        return {VideoExecutionPath::SmartGopCopy, "hybrid_gop_source", true,
                GopExecutionPlan{.safe_to_splice = false}};
    }

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
