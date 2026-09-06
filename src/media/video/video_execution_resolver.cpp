#include <chronon3d/media/video/video_execution_resolver.hpp>

namespace chronon3d::media {
namespace {

/// Internal path classification retained only to preserve the resolver's
/// documented downgrade semantics while deciding.  NOT part of the public
/// decision: the executor consumes the orthogonal VideoExecutionPlan axes.
enum class ResolvedPath : unsigned char {
    BitstreamCopy,
    SmartGopCopy,
    DirectYuv,
    RenderGraph,
};

VideoExecutionPlan direct_yuv_plan() noexcept {
    return {DecodePath::Nvdec, CompositePath::DirectYuv,
            EncodePath::Nvenc, InteropPath::CudaNative,
            SurfaceHandoffPath::Direct};
}

VideoExecutionPlan packet_copy_plan() noexcept {
    return {DecodePath::PacketCopy, CompositePath::None,
            EncodePath::PacketCopy, InteropPath::None,
            SurfaceHandoffPath::None};
}

VideoExecutionPlan software_graph_plan() noexcept {
    return {DecodePath::Software, CompositePath::SoftwareGraph,
            EncodePath::Pipe, InteropPath::None,
            SurfaceHandoffPath::HostUpload};
}

struct Decision {
    VideoExecutionPlan plan{};
    ResolvedPath path{ResolvedPath::RenderGraph};
    ResolvedPath render_fallback{ResolvedPath::RenderGraph};
    std::string_view reason{"render_graph_default"};
    bool valid{true};
    std::optional<GopExecutionPlan> gop_plan{};
};

VideoExecutionDecision to_decision(const Decision& d) noexcept {
    VideoExecutionDecision result;
    result.plan = d.plan;
    result.reason = d.reason;
    result.valid = d.valid;
    result.gop_plan = d.gop_plan;
    return result;
}

// Legacy flag semantics, kept internal to the resolver. Authoring layers
// express intent through ExecutionRequirements / GopAnalysisSummary; the
// old per-path views were demolished when the CLI executor moved to the
// final-plan contract (audit Wave 1).
Decision resolve_legacy_internal(
    bool encoder_native,
    bool hardware_nvenc,
    GpuHotPathMode hot_path,
    bool has_gop_source,
    bool gop_copy_only,
    bool allow_hybrid_gop) noexcept {
    const bool native_nvenc = encoder_native && hardware_nvenc;

    if (has_gop_source && gop_copy_only) {
        return {.plan = packet_copy_plan(),
                .path = ResolvedPath::BitstreamCopy,
                .render_fallback = ResolvedPath::DirectYuv,
                .reason = "explicit_gop_copy",
                .valid = true,
                .gop_plan = GopExecutionPlan{.all_copy = true,
                                             .safe_to_splice = true}};
    }

    // Smart GOP hybrid intent. The resolver declares that a hybrid plan IS
    // the right path; per-GOP analysis still happens at execution time (the
    // preflight that inspects the compressed source), so safe-to-splice is
    // deferred. A source that fails inspection is downgraded to DirectYuv by
    // the analysis-aware entry point before execution.
    if (has_gop_source && allow_hybrid_gop && native_nvenc) {
        return {.plan = direct_yuv_plan(),
                .path = ResolvedPath::SmartGopCopy,
                .render_fallback = ResolvedPath::DirectYuv,
                .reason = "hybrid_gop_source",
                .valid = true,
                .gop_plan = GopExecutionPlan{.safe_to_splice = false}};
    }

    if (hot_path == GpuHotPathMode::RequireDirectYuv) {
        if (!native_nvenc) {
            return {.plan = direct_yuv_plan(),
                    .path = ResolvedPath::DirectYuv,
                    .render_fallback = ResolvedPath::RenderGraph,
                    .reason = "direct_yuv_requires_native_nvenc",
                    .valid = false};
        }
        return {.plan = direct_yuv_plan(),
                .path = ResolvedPath::DirectYuv,
                .render_fallback = ResolvedPath::RenderGraph,
                .reason = "required_direct_yuv",
                .valid = true};
    }
    // RequireGpuNative means FullGraph on Vulkan with native GPU encoding. It
    // must never be interpreted as DirectYuv: FullGraph is required for text,
    // image overlays and any other non-trivial composition.
    if (hot_path == GpuHotPathMode::RequireGpuNative) {
        return {.plan = {DecodePath::Nvdec, CompositePath::VulkanGraph,
                         EncodePath::Nvenc, InteropPath::VulkanCuda,
                         SurfaceHandoffPath::VulkanCopy},
                .path = ResolvedPath::RenderGraph,
                .render_fallback = ResolvedPath::RenderGraph,
                .reason = "required_gpu_native_full_graph",
                .valid = true};
    }
    // Auto is a resolver decision, not an alias for FullGraph. The direct
    // program performs the composition-specific eligibility check after this
    // decision and fails closed when the compiled scene is unsupported.
    if (native_nvenc) {
        return {.plan = direct_yuv_plan(),
                .path = ResolvedPath::DirectYuv,
                .render_fallback = ResolvedPath::RenderGraph,
                .reason = "auto_direct_yuv_candidate",
                .valid = true};
    }
    return {.plan = software_graph_plan(),
            .path = ResolvedPath::RenderGraph,
            .render_fallback = ResolvedPath::RenderGraph,
            .reason = "render_graph_default",
            .valid = true};
}

Decision resolve_from_requirements(
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
    Decision result = resolve_legacy_internal(
        capabilities.nvenc,
        capabilities.nvenc,
        hot_path,
        has_gop_source && requirements.packet_copy_allowed,
        gop_copy_only && requirements.packet_copy_allowed,
        allow_hybrid_gop && requirements.packet_copy_allowed);

    if (!requirements.gpu_required) {
        // Auto is allowed to use a discovered accelerator only when the
        // caller did not require a CPU-safe composition. A composition
        // requirement is satisfied by the software graph in this contract.
        if (requirements.composition_required &&
            result.path == ResolvedPath::RenderGraph) {
            result.plan = software_graph_plan();
            result.path = ResolvedPath::RenderGraph;
            result.render_fallback = ResolvedPath::RenderGraph;
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

} // namespace

VideoExecutionDecision resolve_video_execution(
    const ExecutionRequirements& requirements,
    const OutputSpec& output,
    const VideoCapabilities& capabilities,
    bool has_gop_source,
    bool gop_copy_only,
    bool allow_hybrid_gop) noexcept {
    return to_decision(resolve_from_requirements(
        requirements, output, capabilities,
        has_gop_source, gop_copy_only, allow_hybrid_gop));
}

VideoExecutionDecision resolve_video_execution_with_gop_analysis(
    const ExecutionRequirements& requirements,
    const OutputSpec& output,
    const VideoCapabilities& capabilities,
    bool intent_gop_copy_only,
    bool intent_allow_hybrid_gop,
    const GopAnalysisSummary& analysis) noexcept {
    // Explicit whole-clip copy request: the plan is BitstreamCopy only when
    // the preflight proves every GOP is copy-eligible AND splice-safe. Any
    // other outcome is a FINAL structured downgrade — the executor performs
    // no second resolution and no runtime fallback.
    if (intent_gop_copy_only) {
        if (analysis.available && analysis.all_copy) {
            GopExecutionPlan gop;
            gop.total_gops = analysis.total_gops;
            gop.copy_gops = analysis.copy_gops;
            gop.reencode_gops = analysis.reencode_gops;
            gop.all_copy = true;
            gop.hybrid = false;
            gop.safe_to_splice = true;
            VideoExecutionDecision decision;
            decision.plan = packet_copy_plan();
            decision.reason = "explicit_gop_copy";
            decision.valid = true;
            decision.gop_plan = gop;
            return decision;
        }
        // Preflight failed (or the source could not be inspected): downgrade
        // to the direct-YUV plan with a structured reason.
        VideoExecutionDecision decision;
        decision.plan = direct_yuv_plan();
        decision.reason = analysis.available
            ? "gop_copy_preflight_failed"
            : "gop_copy_preflight_unavailable";
        decision.valid = false;
        return decision;
    }

    // Hybrid/SmartGOP intent (gop source present, copy not forced).
    const bool native_nvenc = capabilities.nvenc;
    if (intent_allow_hybrid_gop && native_nvenc) {
        // The whole source is copy-eligible: route to a whole-clip copy.
        if (analysis.available && analysis.all_copy) {
            GopExecutionPlan gop;
            gop.total_gops = analysis.total_gops;
            gop.copy_gops = analysis.copy_gops;
            gop.reencode_gops = analysis.reencode_gops;
            gop.all_copy = true;
            gop.hybrid = false;
            gop.safe_to_splice = true;
            VideoExecutionDecision decision;
            decision.plan = packet_copy_plan();
            decision.reason = "gop_all_copy_eligible";
            decision.valid = true;
            decision.gop_plan = gop;
            return decision;
        }
        VideoExecutionDecision decision;
        decision.plan = direct_yuv_plan();
        decision.valid = false;
        if (!analysis.available) {
            // The compressed source could not be inspected; fail closed to
            // DirectYuv (same lane as a failed splice gate) instead of
            // silently rendering a full graph the caller did not expect.
            decision.reason = "gop_inspection_failed";
        } else if (analysis.hybrid && !analysis.safe_to_splice) {
            // Copy-eligible GOPs exist but splicing source+NVENC packets
            // would corrupt the decoder: the whole clip must re-encode.
            decision.reason = "gop_splice_unsafe";
        } else if (analysis.hybrid) {
            // Hybrid is splice-safe but the hybrid executor is not shipped:
            // the resolver downgrades up front with a structured reason (the
            // executor never logs-and-downgrades at runtime).
            decision.reason = "hybrid_pipeline_unavailable";
        } else {
            decision.reason = "gop_reencode_required";
        }
        return decision;
    }

    // No GOP path applies (no compressed source intent or no native NVENC):
    // normal requirements/capabilities resolution.
    return resolve_video_execution(
        requirements, output, capabilities,
        false, false, false);
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
