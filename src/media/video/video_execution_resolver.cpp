#include <chronon3d/media/video/video_execution_resolver.hpp>

namespace chronon3d::media {
namespace {

bool valid_output_spec(const OutputSpec& output) noexcept {
    // Zero is the sentinel for dimensions/frame rate supplied by the
    // composition. Partial values are not meaningful and must not be
    // silently ignored by the resolver.
    if ((output.width == 0) != (output.height == 0)) return false;
    if (output.fps_den == 0) return false;
    if (output.codec.empty() || output.codec == "auto" ||
        output.codec == "h264" || output.codec == "h265" ||
        output.codec == "hevc" || output.codec == "vp9" ||
        output.codec == "av1") {
        return true;
    }
    return false;
}

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
    std::uint32_t downgrade_count{0};
    std::optional<GopExecutionPlan> gop_plan{};
    VideoExecutionPath codec_path{VideoExecutionPath::FullReencode};
};

VideoExecutionDecision to_decision(const Decision& d) noexcept {
    VideoExecutionDecision result;
    result.plan = d.plan;
    result.reason = d.reason;
    result.valid = d.valid;
    result.downgrade_count = d.downgrade_count;
    result.gop_plan = d.gop_plan;
    result.codec_path = d.codec_path;
    return result;
}

// Resolve the final orthogonal plan from semantic capabilities and intents.
// The executor consumes only VideoExecutionPlan; this helper is intentionally
// private so there is one public resolver authority and no compatibility view.
Decision resolve_plan_from_capabilities(
    bool encoder_native,
    bool hardware_nvenc,
    GpuHotPathMode hot_path,
    bool has_gop_source,
    bool gop_copy_only,
    bool allow_hybrid_gop) noexcept {
    const bool native_nvenc = encoder_native && hardware_nvenc;

    // An explicit GPU-native composition is stronger than GOP-copy intent.
    // A gop source is still needed for audio/muxing, but allowing the hybrid
    // shortcut here would silently select DirectYuv and drop other visual
    // layers (for example the foreground over a video background).
    if (hot_path == GpuHotPathMode::RequireGpuNative) {
        return {.plan = {DecodePath::Nvdec, CompositePath::VulkanGraph,
                         EncodePath::Nvenc, InteropPath::VulkanCuda,
                         SurfaceHandoffPath::Direct},
                .path = ResolvedPath::RenderGraph,
                .render_fallback = ResolvedPath::RenderGraph,
                .reason = "required_gpu_native_full_graph",
                .valid = true};
    }

    if (has_gop_source && gop_copy_only) {
        // Codec boundary decided by the shared table, never hard-coded here:
        // an untouched, codec-compatible segment copies its packets.
        return {.plan = packet_copy_plan(),
                .path = ResolvedPath::BitstreamCopy,
                .render_fallback = ResolvedPath::DirectYuv,
                .reason = "explicit_gop_copy",
                .valid = true,
                .gop_plan = GopExecutionPlan{.all_copy = true,
                                             .safe_to_splice = true},
                .codec_path = resolve_video_execution_path(
                    /*content_changed=*/false, /*boundary_change=*/false,
                    /*codec_compatible=*/true, /*splice_safe=*/true)};
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
                .gop_plan = GopExecutionPlan{.safe_to_splice = false},
                // Preflight is deferred: the intent is a boundary-only
                // re-encode, and safe_to_splice=false records that the
                // compressed-source inspection must still confirm it. The
                // final entry point replaces this with the proven path.
                .codec_path = resolve_video_execution_path(
                    /*content_changed=*/true, /*boundary_change=*/true,
                    /*codec_compatible=*/true, /*splice_safe=*/true)};
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
    if (!valid_output_spec(output)) {
        return {.plan = software_graph_plan(),
                .path = ResolvedPath::RenderGraph,
                .render_fallback = ResolvedPath::RenderGraph,
                .reason = "invalid_output_spec",
                .valid = false};
    }

    // A composition without a source video cannot feed the native NVENC
    // surface lane. It may still use the GPU compositor, but the final
    // handoff is a host frame to the pipe encoder. This is the semantic
    // meaning of video_source_required=false and must be decided here rather
    // than inferred later by an executor.
    if (requirements.composition_required && !requirements.video_source_required) {
        if (requirements.gpu_required) {
            if (!capabilities.vulkan_graph) {
                return {.plan = software_graph_plan(),
                        .path = ResolvedPath::RenderGraph,
                        .render_fallback = ResolvedPath::RenderGraph,
                        .reason = "required_gpu_capability_unavailable",
                        .valid = false};
            }
            return {.plan = {DecodePath::Software, CompositePath::VulkanGraph,
                             EncodePath::Pipe, InteropPath::None,
                             SurfaceHandoffPath::HostUpload},
                    .path = ResolvedPath::RenderGraph,
                    .render_fallback = ResolvedPath::RenderGraph,
                    .reason = "gpu_composition_pipe_no_video_source",
                    .valid = true};
        }
        if (!requirements.cpu_fallback_allowed) {
            return {.plan = software_graph_plan(),
                    .path = ResolvedPath::RenderGraph,
                    .render_fallback = ResolvedPath::RenderGraph,
                    .reason = "cpu_fallback_forbidden",
                    .valid = false};
        }
        return {.plan = software_graph_plan(),
                .path = ResolvedPath::RenderGraph,
                .render_fallback = ResolvedPath::RenderGraph,
                .reason = "software_composition_no_video_source",
                .valid = true};
    }

    // Keep backend naming inside Chronon. The caller-facing overload exposes
    // only requirements, output and discovered capabilities.
    const auto hot_path = requirements.gpu_required
        ? (requirements.composition_required
            ? GpuHotPathMode::RequireGpuNative
            : GpuHotPathMode::RequireDirectYuv)
        : GpuHotPathMode::Auto;
    Decision result = resolve_plan_from_capabilities(
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
        if (!requirements.cpu_fallback_allowed && !result.plan.uses_gpu()) {
            result.valid = false;
            result.reason = "cpu_fallback_forbidden";
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

VideoExecutionPath resolve_video_execution_path_from_gop_analysis(
    bool intent_gop_copy_only,
    bool intent_allow_hybrid_gop,
    const GopAnalysisSummary& analysis) noexcept {
    if (!intent_gop_copy_only && !intent_allow_hybrid_gop) {
        // No bitstream-reuse intent: the source is not a copy candidate.
        return VideoExecutionPath::FullReencode;
    }
    if (intent_gop_copy_only && !(analysis.available && analysis.all_copy)) {
        // Explicit copy intent the preflight cannot prove: fail closed.
        return VideoExecutionPath::FullReencode;
    }
    // A preflight that found every GOP copy-eligible is a trim when the
    // requested window is narrower than the source; only the full source is
    // a whole-clip copy. Otherwise the change is interior-or-boundary and the
    // predicate table decides.
    const bool reusable = analysis.available && analysis.all_copy;
    if (reusable && analysis.trim_only) {
        return VideoExecutionPath::TrimPackets;
    }
    const bool content_changed = intent_allow_hybrid_gop && !reusable;
    return resolve_video_execution_path(
        /*content_changed=*/content_changed,
        /*boundary_change=*/content_changed && analysis.available && analysis.hybrid,
        /*codec_compatible=*/analysis.available,
        /*splice_safe=*/analysis.available && analysis.safe_to_splice);
}

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
    // Single codec-boundary authority: the pure table maps the GOP facts to
    // the reuse path. Branches below either report that path (when they copy
    // packets) or FullReencode (when the final plan re-encodes every frame);
    // no branch invents the reuse decision from capabilities.
    const VideoExecutionPath boundary_path =
        resolve_video_execution_path_from_gop_analysis(
            intent_gop_copy_only, intent_allow_hybrid_gop, analysis);

    // Composition is an explicit visual requirement. GOP copy/hybrid intent
    // may still provide audio timing, but it must not replace the graph with
    // DirectYuv or packet-copy execution and lose authored layers. A verified
    // hybrid schedule is still useful here: the changed GOPs use the required
    // graph while untouched GOPs remain eligible for packet copy.
    if (requirements.gpu_required && requirements.composition_required) {
        VideoExecutionDecision decision = resolve_video_execution(
            requirements, output, capabilities, false, false, false);
        const bool preserved_hybrid =
            !intent_gop_copy_only && intent_allow_hybrid_gop &&
            analysis.available && analysis.hybrid && analysis.safe_to_splice &&
            analysis.valid_schedule();
        if (preserved_hybrid) {
            GopExecutionPlan gop;
            gop.total_gops = analysis.total_gops;
            gop.copy_gops = analysis.copy_gops;
            gop.reencode_gops = analysis.reencode_gops;
            gop.all_copy = false;
            gop.hybrid = true;
            gop.safe_to_splice = true;
            gop.work_units = analysis.work_units;
            gop.source_time_base_num = analysis.source_time_base_num;
            gop.source_time_base_den = analysis.source_time_base_den;
            gop.trim_only = analysis.trim_only;
            decision.gop_plan = std::move(gop);
            decision.reason = "hybrid_gop_full_graph";
            decision.codec_path = VideoExecutionPath::BoundaryReencode;
        } else {
            decision.codec_path = VideoExecutionPath::FullReencode;
        }
        return decision;
    }

    // Explicit whole-clip copy request: the plan is BitstreamCopy only when
    // the preflight proves every GOP is copy-eligible AND splice-safe. Any
    // other outcome is a FINAL structured downgrade — the executor performs
    // no second resolution and no runtime fallback.
    if (intent_gop_copy_only) {
        if (analysis.available && !analysis.valid_schedule()) {
            VideoExecutionDecision decision;
            decision.plan = direct_yuv_plan();
            decision.reason = "gop_schedule_invalid";
            decision.valid = false;
            decision.downgrade_count = 1;
            decision.codec_path = VideoExecutionPath::FullReencode;
            return decision;
        }
        if (analysis.available && analysis.all_copy) {
            GopExecutionPlan gop;
            gop.total_gops = analysis.total_gops;
            gop.copy_gops = analysis.copy_gops;
            gop.reencode_gops = analysis.reencode_gops;
            gop.all_copy = true;
            gop.hybrid = false;
            gop.safe_to_splice = true;
            gop.work_units = analysis.work_units;
            gop.source_time_base_num = analysis.source_time_base_num;
            gop.source_time_base_den = analysis.source_time_base_den;
            gop.trim_only = analysis.trim_only;
            VideoExecutionDecision decision;
            decision.plan = packet_copy_plan();
            decision.reason = "explicit_gop_copy";
            decision.valid = true;
            decision.gop_plan = gop;
            decision.codec_path = boundary_path;
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
        decision.downgrade_count = 1;
        decision.codec_path = VideoExecutionPath::FullReencode;
        return decision;
    }

    // Hybrid/SmartGOP intent (gop source present, copy not forced).
    const bool native_nvenc = capabilities.nvenc;
    if (intent_allow_hybrid_gop && native_nvenc) {
        if (analysis.available && !analysis.valid_schedule()) {
            VideoExecutionDecision decision;
            decision.plan = direct_yuv_plan();
            decision.reason = "gop_schedule_invalid";
            decision.valid = false;
            decision.downgrade_count = 1;
            decision.codec_path = VideoExecutionPath::FullReencode;
            return decision;
        }
        // The whole source is copy-eligible: route to a whole-clip copy.
        if (analysis.available && analysis.all_copy) {
            GopExecutionPlan gop;
            gop.total_gops = analysis.total_gops;
            gop.copy_gops = analysis.copy_gops;
            gop.reencode_gops = analysis.reencode_gops;
            gop.all_copy = true;
            gop.hybrid = false;
            gop.safe_to_splice = true;
            gop.work_units = analysis.work_units;
            gop.source_time_base_num = analysis.source_time_base_num;
            gop.source_time_base_den = analysis.source_time_base_den;
            gop.trim_only = analysis.trim_only;
            VideoExecutionDecision decision;
            decision.plan = packet_copy_plan();
            decision.reason = "gop_all_copy_eligible";
            decision.valid = true;
            decision.gop_plan = gop;
            decision.codec_path = boundary_path;
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
        decision.downgrade_count = 1;
        decision.gop_plan = GopExecutionPlan{
            .total_gops = analysis.total_gops,
            .copy_gops = analysis.copy_gops,
            .reencode_gops = analysis.reencode_gops,
            .all_copy = analysis.all_copy,
            .hybrid = analysis.hybrid,
            .safe_to_splice = analysis.safe_to_splice,
            .work_units = analysis.work_units,
            .source_time_base_num = analysis.source_time_base_num,
            .source_time_base_den = analysis.source_time_base_den,
            .trim_only = analysis.trim_only};
        // The boundary ideal may be BoundaryReencode, but the shipped plan
        // re-encodes every frame (DirectYuv); the FINAL codec path is
        // therefore FullReencode and `downgrade_count` records the loss.
        decision.codec_path = VideoExecutionPath::FullReencode;
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
    params.cpu_fallback_forbidden = !requirements.cpu_fallback_allowed;
    if (requirements.gpu_required) {
        params.backend = (requested_backend != kDefaultBackend &&
                          !requested_backend.empty())
            ? std::string{requested_backend}
            : ((daemon_backend != kDefaultBackend && !daemon_backend.empty())
                   ? std::string{daemon_backend}
                   : std::string{kDefaultGpuBackend});
        // A strict GPU request is an explicit native-lane contract, including
        // image/text-only compositions.  The old test only looked at the
        // source-video case and treated every composition without a source as
        // a host-frame pipe job.  That made RenderingGen's strict native
        // request arrive at libx264, where its NVENC preset (p2, p3, ...)
        // failed before the first frame.  CPU fallback remains the deliberate
        // escape hatch for the non-strict composition case.
        const bool strict_gpu_native = !requirements.cpu_fallback_allowed;
        const bool pipe_composition = requirements.composition_required &&
            !requirements.video_source_required && !strict_gpu_native;
        params.hardware_encoder = pipe_composition ? "none" : "nvenc";
        // Phase 9 CLI/IPC parity: a composition with no source video is a
        // GPU-composited host-frame job, not a native NVENC surface job.
        // This is a native-surface job; the compiled-program classifier may
        // still select DirectYUV for certified simple overlays or FullGraph
        // for richer compositions.
        if (pipe_composition) {
            params.encoder_backend = std::string(kEncoderTransportPipe);
            params.gpu_hot_path_mode = "auto";
        } else {
            params.encoder_backend = std::string(kEncoderTransportNative);
            // CompositionRequired is a semantic requirement, not a render
            // path selection.  Keep the request on the automatic lane so
            // the compiled composition can be classified by Chronon after
            // compilation: certified simple overlays use DirectYUV, while
            // masks/effects/3D resolve to FullGraph.  RenderingGen must not
            // create a second copy of that capability matrix.
            params.gpu_hot_path_mode = strict_gpu_native &&
                requirements.composition_required
                ? "require_gpu_native" : "auto";
        }
        params.native_nvenc = !pipe_composition;
        params.direct_yuv = !pipe_composition && !requirements.composition_required;
        params.codec = requested_codec.empty()
            ? std::string{kDefaultCodec} : std::string{requested_codec};
    } else {
        params.backend = (requested_backend != kDefaultBackend &&
                          !requested_backend.empty())
            ? std::string{requested_backend}
            : ((daemon_backend != kDefaultBackend && !daemon_backend.empty())
                   ? std::string{daemon_backend}
                   : std::string{kDefaultBackend});
        params.hardware_encoder = "none";
        params.encoder_backend = "pipe";
        params.gpu_hot_path_mode = "auto";
        params.native_nvenc = false;
        params.direct_yuv = false;
        params.codec = requested_codec.empty()
            ? std::string{kAutoCodec} : std::string{requested_codec};
    }
    return params;
}

} // namespace chronon3d::media
