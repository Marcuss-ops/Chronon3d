#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/detail/video_execution_legacy.hpp>

namespace chronon3d::media {

VideoExecutionDecision detail::resolve_legacy_video_execution(
    const detail::LegacyVideoExecutionRequest& request) noexcept {
    const auto decision = [](VideoExecutionPlan plan,
                             VideoExecutionPath legacy_path,
                             VideoExecutionPath legacy_fallback,
                             std::string_view reason,
                             bool valid,
                             std::optional<GopExecutionPlan> gop = std::nullopt) {
        VideoExecutionDecision result;
        result.plan = plan;
        result.path = legacy_path;
        result.render_fallback = legacy_fallback;
        result.reason = reason;
        result.valid = valid;
        result.gop_plan = gop;
        return result;
    };

    // Whole-clip BitstreamCopy: explicit request to copy every packet.
    // The caller (video_job_execute.cpp) routes directly to
    // copy_gop_source() without touching NVDEC/CUDA/NVENC.
    if (request.has_gop_source && request.gop_copy_only) {
        return decision({DecodePath::PacketCopy, CompositePath::None,
                         EncodePath::PacketCopy, InteropPath::None,
                         SurfaceHandoffPath::None},
                        VideoExecutionPath::BitstreamCopy,
                        VideoExecutionPath::DirectYuv, "explicit_gop_copy", true,
                        GopExecutionPlan{.all_copy = true, .safe_to_splice = true});
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
        return decision({DecodePath::Nvdec, CompositePath::DirectYuv,
                         EncodePath::Nvenc, InteropPath::CudaNative,
                         SurfaceHandoffPath::Direct},
                        VideoExecutionPath::SmartGopCopy,
                        VideoExecutionPath::DirectYuv, "hybrid_gop_source", true,
                        GopExecutionPlan{.safe_to_splice = false});
    }

    if (request.hot_path == GpuHotPathMode::RequireDirectYuv) {
        if (!native_nvenc) {
            return decision({DecodePath::Nvdec, CompositePath::DirectYuv,
                             EncodePath::Nvenc, InteropPath::CudaNative,
                             SurfaceHandoffPath::Direct},
                            VideoExecutionPath::DirectYuv,
                            VideoExecutionPath::RenderGraph,
                            "direct_yuv_requires_native_nvenc", false);
        }
        return decision({DecodePath::Nvdec, CompositePath::DirectYuv,
                         EncodePath::Nvenc, InteropPath::CudaNative,
                         SurfaceHandoffPath::Direct},
                        VideoExecutionPath::DirectYuv,
                        VideoExecutionPath::RenderGraph, "required_direct_yuv", true);
    }
    // RequireGpuNative means FullGraph on Vulkan with native GPU encoding. It
    // must never be interpreted as DirectYuv: FullGraph is required for text,
    // image overlays and any other non-trivial composition.
    if (request.hot_path == GpuHotPathMode::RequireGpuNative) {
        return decision({DecodePath::Nvdec, CompositePath::VulkanGraph,
                         EncodePath::Nvenc, InteropPath::VulkanCuda,
                         SurfaceHandoffPath::VulkanCopy},
                        VideoExecutionPath::RenderGraph,
                        VideoExecutionPath::RenderGraph,
                        "required_gpu_native_full_graph", true);
    }
    // Auto is a resolver decision, not an alias for FullGraph. The direct
    // program performs the composition-specific eligibility check after this
    // decision and fails closed when the compiled scene is unsupported.
    if (native_nvenc) {
        return decision({DecodePath::Nvdec, CompositePath::DirectYuv,
                         EncodePath::Nvenc, InteropPath::CudaNative,
                         SurfaceHandoffPath::Direct},
                        VideoExecutionPath::DirectYuv,
                        VideoExecutionPath::RenderGraph,
                        "auto_direct_yuv_candidate", true);
    }
    return decision({DecodePath::Software, CompositePath::SoftwareGraph,
                     EncodePath::Pipe, InteropPath::None,
                     SurfaceHandoffPath::HostUpload},
                    VideoExecutionPath::RenderGraph,
                    VideoExecutionPath::RenderGraph, "render_graph_default", true);
}

VideoExecutionDecision resolve_video_execution(
    const ExecutionRequirements& requirements,
    const OutputSpec& output,
    const VideoCapabilities& capabilities,
    bool has_gop_source,
    bool gop_copy_only,
    bool allow_hybrid_gop) noexcept {
    // Keep backend naming inside Chronon. The caller-facing overload exposes
    // only requirements, output and discovered capabilities.
    const auto hot_path = requirements.gpu_required
        ? (requirements.composition_required
            ? GpuHotPathMode::RequireGpuNative
            : GpuHotPathMode::RequireDirectYuv)
        : GpuHotPathMode::Auto;
    auto result = detail::resolve_legacy_video_execution(detail::LegacyVideoExecutionRequest{
        .encoder_backend = capabilities.nvenc ? "native" : "pipe",
        .hardware_encoder = capabilities.nvenc ? "nvenc" : "",
        .codec = output.codec,
        .hot_path = hot_path,
        .has_gop_source = has_gop_source && requirements.packet_copy_allowed,
        .gop_copy_only = gop_copy_only && requirements.packet_copy_allowed,
        .allow_hybrid_gop = allow_hybrid_gop && requirements.packet_copy_allowed});

    if (!requirements.gpu_required) {
        // Auto is allowed to use a discovered accelerator only when the
        // caller did not require a CPU-safe composition. A composition
        // requirement is satisfied by the software graph in this contract.
        if (requirements.composition_required &&
            result.path == VideoExecutionPath::RenderGraph) {
            result.plan = {DecodePath::Software, CompositePath::SoftwareGraph,
                           EncodePath::Pipe, InteropPath::None,
                           SurfaceHandoffPath::HostUpload};
            result.path = VideoExecutionPath::RenderGraph;
            result.render_fallback = VideoExecutionPath::RenderGraph;
            result.reason = "software_composition_allowed";
        }
        return result;
    }

    if (!capabilities.nvdec || !capabilities.nvenc ||
        (requirements.composition_required && !capabilities.vulkan_graph) ||
        (!requirements.composition_required && !capabilities.cuda_native)) {
        result.valid = false;
        result.reason = "required_gpu_capability_unavailable";
        return result;
    }
    if (!requirements.cpu_fallback_allowed && !result.plan.uses_gpu()) {
        result.valid = false;
        result.reason = "cpu_fallback_forbidden";
    }
    return result;
}

ResolvedExecutionParameters resolve_canonical_execution_parameters(
    const ExecutionRequirements& requirements,
    std::string_view requested_codec,
    std::string_view requested_backend,
    std::string_view daemon_backend) noexcept {
    ResolvedExecutionParameters params;
    if (requirements.gpu_required) {
        params.backend = (requested_backend != "auto" && !requested_backend.empty())
            ? std::string{requested_backend}
            : ((daemon_backend != "auto" && !daemon_backend.empty()) ? std::string{daemon_backend} : "vulkan");
        params.hardware_encoder = "nvenc";
        params.encoder_backend = "native";
        params.gpu_hot_path_mode = requirements.composition_required
            ? "require_gpu_native"
            : "require_direct_yuv";
        params.native_nvenc = true;
        params.direct_yuv = !requirements.composition_required;
        params.cpu_fallback_forbidden = !requirements.cpu_fallback_allowed;
        params.codec = requested_codec.empty() ? "h264" : std::string{requested_codec};
    } else {
        params.backend = (requested_backend != "auto" && !requested_backend.empty())
            ? std::string{requested_backend}
            : ((daemon_backend != "auto" && !daemon_backend.empty()) ? std::string{daemon_backend} : "auto");
        params.hardware_encoder = "none";
        params.encoder_backend = "pipe";
        params.gpu_hot_path_mode = "auto";
        params.native_nvenc = false;
        params.direct_yuv = false;
        params.cpu_fallback_forbidden = false;
        params.codec = requested_codec.empty() ? "auto" : std::string{requested_codec};
    }
    return params;
}

} // namespace chronon3d::media
