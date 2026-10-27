#include "video_export_support.hpp"
#include "video_job_time_rescale.hpp"
#include "video_job_execute_dispatch_detail.hpp"
#include "gop_smart_copy.hpp"
#include "encoder_config_resolution.hpp"
#include "encoder_bitstream_contract.hpp"
#include "gop_target_contract.hpp"
#include "ffmpeg_profile_constants.hpp"
#include "../../commands/video/common/pipe_export_helpers.hpp"
#include "../../commands/video/common/pipe_export_pipeline.hpp"
#include "../../commands/video/exporters/pipe_export_pipeline_internal.hpp"
#include "../../commands/video/common/video_export_common.hpp"
#include <chronon3d/media/video/direct_yuv_program.hpp>
#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/gop_execution_executor.hpp>
#include <chronon3d/media/video/output_contract.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace chronon3d::cli {
namespace {

using video_detail::frame_to_tick;
using video_detail::tick_to_frame;

// ── Single GOP preflight (before any decision) ─────────────────────────────
// Runs once at the top of the executor; the resulting summary is folded into
// the resolver so the FINAL plan already contains any downgrade.  The
// executor must not re-inspect or re-resolve later.
// The analysis → schedule mapping is shared with the preflight and the parity
// gate: `build_gop_plan_from_analysis` (utils/video/gop_smart_copy.hpp). This
// lane uses it with `splice_safe = true`, which is exactly what declaring the
// container FROM the source certifies.



// CLI flag surface → semantic execution requirements.  The public media
// resolver never sees backend strings; the CLI translates its own flags at
// this boundary (the only legacy-flag caller after the resolver closeout).
media::ExecutionRequirements cli_execution_requirements(
    const FfmpegExportOptions& opts) {
    const auto hot = opts.gpu_hot_path_mode;
    return media::ExecutionRequirements{
        .gpu_required = hot == GpuHotPathMode::RequireDirectYuv ||
                        hot == GpuHotPathMode::RequireGpuNative,
        .cpu_fallback_allowed = true,
        // FullGraph composition is required only for the explicit Vulkan
        // graph mode; DirectYuv composes overlays in the NV12 domain and the
        // Auto lane follows the legacy direct-candidate/default semantics.
        .composition_required = hot == GpuHotPathMode::RequireGpuNative ||
                                !opts.pipe.ocio_display.empty(),
        .packet_copy_allowed = true,
    };
}

media::VideoCapabilities cli_effective_capabilities(
    const FfmpegExportOptions& opts) {
    const bool native_nvenc =
        is_native_encoder_transport(opts.encoder.encoder_backend) &&
        hardware_nvenc_requested(opts.encoder.hardware_encoder);
    return media::VideoCapabilities{
        .nvdec = native_nvenc,
        .nvenc = native_nvenc,
        .vulkan_graph =
            opts.gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
            opts.backend_preference == graph::BackendPreference::GPU,
        .cuda_native = native_nvenc,
    };
}

// The semantic resolver can only select a DirectYUV candidate before it sees
// the compiled scene. This is the single Chronon-owned composition
// classification step that turns that candidate into the final plan. The
// lower-level pipeline receives the result and must not repeat this decision.
media::VideoExecutionDecision finalize_composition_execution(
    const CompiledComposition& compiled,
    const FfmpegExportOptions& opts,
    media::VideoExecutionDecision decision) {
    if (decision.plan.composite != media::CompositePath::DirectYuv) {
        return decision;
    }

    const auto policy = opts.gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv
        ? media::video::DirectYuvPolicy::Require
        : media::video::DirectYuvPolicy::Auto;
    const auto capability = media::video::DirectYuvProgram::resolve_support(
        compiled, policy);
    if (capability.eligible) {
        return decision;
    }

    spdlog::info(
        "[video] DirectYUV composition classification rejected the candidate: {}",
        capability.reason);
    if (policy == media::video::DirectYuvPolicy::Require) {
        decision.valid = false;
        decision.reason = "direct_yuv_capability_required";
        return decision;
    }

    // Auto is an explicit resolver outcome: FullGraph is the final plan, not
    // a runtime fallback discovered by the exporter.
    decision.plan = detail::render_graph_fallback_plan(opts);
    decision.reason = "direct_yuv_capability_rejected";
    decision.valid = true;
    decision.downgrade_count = 1;
    // The render-graph plan re-encodes every frame, so the reported codec
    // boundary stays consistent with the final plan.
    decision.codec_path = media::VideoExecutionPath::FullReencode;
    return decision;
}

} // namespace

int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoJobExecutionContext> execution,
    std::chrono::steady_clock::time_point* sidecar_written_at,
    std::shared_ptr<SoftwareRenderer> warm_renderer) {
    if (opts.output.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (end <= start) {
        spdlog::error("[video] Empty frame range [{}, {})", start, end);
        return 1;
    }
    if (opts.sink.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unsupported ffmpeg mode '{}'. Only 'pipe' is available",
                      opts.sink.ffmpeg_mode);
        return 1;
    }
    // Ingress contract: a supplied compressed source must be readable before
    // any resolver downgrade can silently replace the requested copy/re-encode
    // lane with a different plan (see gop_copy_preflight_unavailable).
    if (!opts.gop_source.empty()) {
        std::error_code ec;
        const bool readable = std::filesystem::is_regular_file(opts.gop_source, ec)
            && !ec;
        if (!readable) {
            spdlog::error("[video] gop_source '{}' is not a readable regular file (ec={})",
                          opts.gop_source, ec ? ec.message() : "none");
            return 1;
        }
    }

    const double fps_val = opts.output.fps_value();
    const double safe_fps = fps_val > 0.0 ? fps_val : 30.0;
    const double start_sec = static_cast<double>(start.integral()) / safe_fps;
    const double end_sec = static_cast<double>(end.integral()) / safe_fps;

    // ── ONE final, immutable plan ──────────────────────────────────────────
    // Requirements + capabilities + GOP pre-analysis → resolver → final plan.
    // The executor below only dispatches on the plan axes; it never decides
    // policy and never downgrades at runtime (audit Wave 1 closeout).
    auto render_opts = opts;
    if (opts.gop_source.empty()) {
        auto decision = media::resolve_video_execution(
            cli_execution_requirements(opts), media::OutputSpec{},
            cli_effective_capabilities(opts));
        decision = finalize_composition_execution(compiled, opts, decision);
        render_opts.resolved_execution_plan = decision.plan;
        render_opts.resolved_execution_reason = std::string{decision.reason};
        render_opts.resolved_execution_valid = decision.valid;
        render_opts.resolved_execution_downgrade_count = decision.downgrade_count;
        spdlog::info("[video] execution plan resolved (reason: {}); decode={} composite={} encode={} interop={} handoff={} codec={}",
                     decision.reason,
                     static_cast<int>(decision.plan.decode),
                     static_cast<int>(decision.plan.composite),
                     static_cast<int>(decision.plan.encode),
                     static_cast<int>(decision.plan.interop),
                     static_cast<int>(decision.plan.handoff),
                     static_cast<int>(decision.codec_path));
    } else {
        const media::GopAnalysisSummary summary = detail::inspect_source_once(
            opts, compiled, start_sec, end_sec, cpu_budget);
        if (summary.available) {
            spdlog::info("[video] gop preflight: {} GOPs ({} copy-eligible, {} re-encode{}), splice_safe={}",
                         summary.total_gops, summary.copy_gops,
                         summary.reencode_gops,
                         summary.all_copy ? ", all-copy" :
                             (summary.hybrid ? ", hybrid" : ""),
                         summary.safe_to_splice);
        }
        auto decision = media::resolve_video_execution_with_gop_analysis(
            cli_execution_requirements(opts), media::OutputSpec{},
            cli_effective_capabilities(opts),
            opts.gop_copy_only,
            !opts.gop_copy_only,
            summary);
        decision = finalize_composition_execution(compiled, opts, decision);
        render_opts.resolved_execution_plan = decision.plan;
        render_opts.resolved_execution_reason = std::string{decision.reason};
        render_opts.resolved_execution_valid = decision.valid;
        render_opts.resolved_execution_downgrade_count = decision.downgrade_count;
        spdlog::info("[video] execution plan resolved (reason: {}); decode={} composite={} encode={} interop={} handoff={} codec={}",
                     decision.reason,
                     static_cast<int>(decision.plan.decode),
                     static_cast<int>(decision.plan.composite),
                     static_cast<int>(decision.plan.encode),
                     static_cast<int>(decision.plan.interop),
                     static_cast<int>(decision.plan.handoff),
                     static_cast<int>(decision.codec_path));
    }

    // ── Enforce the resolver validity contract ─────────────────────────────
    // `valid=false` covers two different situations that the dispatcher must
    // distinguish:
    //   * capability-denied reasons: the requested lane cannot execute on this
    //     host at all (no NVDEC/NVENC/Vulkan/CUDA, CPU fallback forbidden).
    //     Fail fast here instead of dispatching a plan that can only fail
    //     after renderer/encoder setup.
    //   * structured downgrade reasons (gop_*): the downgraded plan IS the
    //     intended executable outcome; run it, but surface the downgrade at
    //     warn level so it is not a silent re-encode.
    if (render_opts.resolved_execution_valid == false) {
        const auto& reason = render_opts.resolved_execution_reason;
        const bool capability_denied =
            reason == "required_gpu_capability_unavailable" ||
            reason == "cpu_fallback_forbidden" ||
            reason == "direct_yuv_requires_native_nvenc" ||
            reason == "direct_yuv_capability_required";
        if (capability_denied) {
            spdlog::error(
                "[video] FAIL_CLOSED: requested execution cannot run on this host "
                "(resolver reason: {}); aborting before setup", reason);
            return 1;
        }
        spdlog::warn(
            "[video] final plan is a structured downgrade (resolver reason: {}); "
            "executing the downgraded axes (count={})", reason,
            render_opts.resolved_execution_downgrade_count);
    }

    // ── Whole-clip packet copy (final plan: PacketCopy both ways) ─────────
    // Reached only when the GOP preflight proved every packet is
    // copy-eligible and splice-safe; no runtime downgrade exists on this
    // lane anymore.
    if (const auto packet_copy_status = detail::execute_packet_copy_lane(
            render_opts, start_sec, end_sec)) {
        return *packet_copy_status;
    }

    // ── Hybrid GOP: direct mux (P0-3 phase 2 / P0-5E / P0-5F) ─────────────
    // A caller that supplied a compressed source with hybrid intent reuses
    // every untouched GOP verbatim. The encoder-owned preflight cannot certify
    // a copy before an encoder exists (P0-5A), so this lane declares the
    // container FROM the source and certifies the ENCODER against that
    // declaration instead: copied packets are the output stream by
    // construction, and a re-encoded unit that does not reproduce the stream
    // fails the export closed. When the schedule is not a hybrid one the
    // resolved plan runs unchanged — the lane is refused, never guessed.
    if (!opts.gop_source.empty() && !opts.gop_copy_only) {
        std::string direct_mux_reason;
        if (detail::execute_direct_mux_gop_plan(
                registry, compiled, composition_id, settings, start, end,
                render_opts, cpu_budget, execution, direct_mux_reason)) {
            return 0;
        }
        // The lane declined, or it could not certify itself. It publishes
        // nothing on any failure path (the partial container is removed), and
        // the fallback is the plan the resolver ALREADY chose for this job:
        // for a hybrid-intent caller that is the structured
        // `gop_reencode_required` downgrade, not a new decision taken here.
        // Surfaced at warn level so a declined optimization is never silent.
        spdlog::warn(
            "[direct-mux] hybrid lane not used ({}); executing the resolved "
            "plan instead",
            direct_mux_reason);
    }

    // ── Render path: dispatch purely on the final plan's composite axis ───
    // DirectYuv ↔ CompositePath::DirectYuv; every other composite (software
    // or Vulkan graph) runs the full render-graph pipeline.  There is no
    // render_fallback view to consult anymore.
    if (render_opts.resolved_execution_plan &&
        render_opts.resolved_execution_plan->composite ==
            media::CompositePath::DirectYuv) {
        render_opts.resolved_execution_path =
            FfmpegExportOptions::ResolvedExecutionPath::DirectYuv;
    } else {
        render_opts.resolved_execution_path =
            FfmpegExportOptions::ResolvedExecutionPath::FullGraph;
    }

    auto* device_scheduler = execution ? execution->device_scheduler : nullptr;
    auto video_runtimes = execution ? execution->video_runtimes : nullptr;
    auto result = render_and_encode_native(
        registry, compiled, composition_id,
        settings, start, end, render_opts, cpu_budget,
        std::move(video_runtimes),
        device_scheduler,
        std::move(execution),
        std::move(warm_renderer));
    if (sidecar_written_at) *sidecar_written_at = result.sidecar_written_at;
    return result.return_code;
}

} // namespace chronon3d::cli
