#include "video_export_support.hpp"
#include "gop_smart_copy.hpp"
#include "../../commands/video/common/pipe_export_pipeline.hpp"
#include "../../commands/video/exporters/pipe_export_pipeline_internal.hpp"
#include "../../commands/video/common/video_export_common.hpp"
#include <chronon3d/media/video/direct_yuv_program.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <algorithm>
#include <filesystem>
#include <utility>

namespace chronon3d::cli {
namespace {

BitstreamTargetContract make_bitstream_target_contract(
    const FfmpegExportOptions& opts, const CompiledComposition& compiled,
    const std::string& source_path) {
    BitstreamTargetContract target;
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if (opts.encoder.codec == "h264" || opts.encoder.codec == "libx264" ||
        opts.encoder.codec == "h264_nvenc") {
        target.codec = AV_CODEC_ID_H264;
    } else if (opts.encoder.codec == "hevc" || opts.encoder.codec == "h265" ||
               opts.encoder.codec == "libx265" || opts.encoder.codec == "hevc_nvenc") {
        target.codec = AV_CODEC_ID_HEVC;
    } else if (opts.encoder.codec == "av1" || opts.encoder.codec == "av1_nvenc") {
        target.codec = AV_CODEC_ID_AV1;
    }
    target.width = static_cast<std::uint32_t>(compiled.composition->width());
    target.height = static_cast<std::uint32_t>(compiled.composition->height());
    target.pixel_format = opts.encoder.encoder_backend == "native" &&
            opts.encoder.hardware_encoder == "nvenc"
        ? AV_PIX_FMT_CUDA : AV_PIX_FMT_YUV420P;

    AVFormatContext* format = nullptr;
    if (!source_path.empty() &&
        avformat_open_input(&format, source_path.c_str(), nullptr, nullptr) >= 0 &&
        avformat_find_stream_info(format, nullptr) >= 0) {
        for (unsigned int index = 0; index < format->nb_streams; ++index) {
            const auto* params = format->streams[index]->codecpar;
            if (params->codec_type != AVMEDIA_TYPE_VIDEO) continue;
            target.codec = params->codec_id;
            target.profile = params->profile;
            target.level = params->level;
            target.width = static_cast<std::uint32_t>(params->width);
            target.height = static_cast<std::uint32_t>(params->height);
            target.pixel_format = static_cast<BitstreamPixelFormat>(params->format);
            if (params->extradata && params->extradata_size > 0) {
                target.parameter_sets.assign(
                    params->extradata, params->extradata + params->extradata_size);
            }
            target.color_range = params->color_range;
            target.color_space = params->color_space;
            target.color_primaries = params->color_primaries;
            target.color_trc = params->color_trc;
            break;
        }
        avformat_close_input(&format);
    }
#else
    (void)opts;
    (void)compiled;
    (void)source_path;
#endif
    return target;
}

// ── Single GOP preflight (before any decision) ─────────────────────────────
// Runs once at the top of the executor; the resulting summary is folded into
// the resolver so the FINAL plan already contains any downgrade.  The
// executor must not re-inspect or re-resolve later.
media::GopAnalysisSummary inspect_source_once(
    const FfmpegExportOptions& opts, const CompiledComposition& compiled,
    double start_sec, double end_sec) {
    media::GopAnalysisSummary summary;
    if (opts.gop_source.empty()) return summary;
    const auto analysis = inspect_gop_source(
        opts.gop_source,
        make_bitstream_target_contract(opts, compiled, opts.gop_source),
        start_sec, end_sec,
        /*content_changed=*/!opts.gop_copy_only,
        opts.changed_intervals);
    if (!analysis) {
        summary.available = false;
        return summary;
    }
    summary.available = true;
    summary.total_gops = analysis->plans.size();
    summary.copy_gops = analysis->copy_count;
    summary.reencode_gops = analysis->reencode_count;
    summary.all_copy = analysis->all_copy_eligible;
    summary.hybrid = analysis->is_hybrid();
    summary.trim_only = analysis->trim_only;
    summary.source_time_base_num = analysis->source_time_base_num;
    summary.source_time_base_den = analysis->source_time_base_den;
    summary.work_units.reserve(analysis->plans.size());
    for (const auto& plan : analysis->plans) {
        const media::TimestampRange source_packets{
            plan.first_dts, plan.last_dts, true};
        const media::TimestampRange decode_range{
            plan.decode_first_dts, plan.last_dts, true};
        const media::TimestampRange render_range{
            plan.first_pts, plan.last_pts, true};
        const bool copy_packets = plan.copy_packets();
        summary.work_units.push_back(media::GopWorkUnit{
            .ordinal = plan.ordinal,
            .first_pts = plan.first_pts,
            .last_pts = plan.last_pts,
            .first_dts = plan.first_dts,
            .last_dts = plan.last_dts,
            .kind = copy_packets
                ? media::GopWorkKind::CopyPackets
                : media::GopWorkKind::Reencode,
            .source_packets = source_packets,
            .decode_range = copy_packets ? media::TimestampRange{}
                                         : decode_range,
            .render_range = copy_packets ? media::TimestampRange{}
                                         : render_range,
            .encode_range = copy_packets ? media::TimestampRange{}
                                         : render_range,
            .force_idr_at_start = !copy_packets,
            .closes_dependencies_at_end = plan.compatibility.random_access_safe,
            .compatibility = plan.compatibility});
    }
    // Splice safety matters only when some GOPs are copied alongside
    // re-encoded ones: every planned Copy must have passed the gate.
    summary.safe_to_splice = summary.hybrid;
    if (summary.safe_to_splice) {
        for (const auto& plan : analysis->plans) {
            if (plan.copy_packets() && !plan.compatibility.safe_to_splice()) {
                summary.safe_to_splice = false;
                break;
            }
        }
    }
    return summary;
}

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
        .composition_required = hot == GpuHotPathMode::RequireGpuNative,
        .packet_copy_allowed = true,
    };
}

media::VideoCapabilities cli_effective_capabilities(
    const FfmpegExportOptions& opts) {
    const bool native_nvenc = opts.encoder.encoder_backend == "native" &&
                              opts.encoder.hardware_encoder == "nvenc";
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
    std::shared_ptr<media::VideoJobExecutionContext> execution) {
    if (opts.output.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (encoder_backend_requires_ffmpeg(opts.encoder.encoder_backend) && !ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH (required by the pipe encoder).");
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
        render_opts.resolved_gop_plan = decision.gop_plan;
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
        const media::GopAnalysisSummary summary = inspect_source_once(
            opts, compiled, start_sec, end_sec);
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
        render_opts.resolved_gop_plan = decision.gop_plan;
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
    if (render_opts.resolved_execution_plan &&
        render_opts.resolved_execution_plan->decode ==
            media::DecodePath::PacketCopy &&
        render_opts.resolved_execution_plan->encode ==
            media::EncodePath::PacketCopy &&
        render_opts.resolved_execution_valid) {
        spdlog::info("[bitstream-copy] path selected (reason: {}); bypassing NVDEC/CUDA/NVENC entirely",
                     render_opts.resolved_execution_reason);
        const auto copy_t0 = profiling::now();
        if (const auto parent = std::filesystem::path(opts.output.output).parent_path();
            !parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                spdlog::error("[bitstream-copy] failed to create output directory '{}': {}",
                              parent.string(), ec.message());
                return 1;
            }
        }

        auto final_path = std::filesystem::path(opts.output.output);
        auto partial_path = final_path;
        const auto ext = final_path.extension();
        if (ext.empty()) partial_path += ".partial";
        else partial_path.replace_filename(
            final_path.stem().string() + ".partial" + ext.string());

        auto copy_result = copy_gop_source(
            opts.gop_source, partial_path.string(), start_sec, end_sec);
        const double copy_ms = profiling::duration_ms(copy_t0, profiling::now());
        if (!copy_result) {
            spdlog::error("[bitstream-copy] copy_gop_source failed for source '{}' → '{}'",
                          opts.gop_source, partial_path.string());
            std::error_code rm_ec;
            std::filesystem::remove(partial_path, rm_ec);
            return 1;
        }
        std::error_code rename_ec;
        std::filesystem::rename(partial_path, final_path, rename_ec);
        if (rename_ec) {
            spdlog::error("[bitstream-copy] failed to rename '{}' → '{}': {}",
                          partial_path.string(), final_path.string(), rename_ec.message());
            return 1;
        }
        spdlog::info("[bitstream-copy] completed: {} video packets, {} audio packets in {:.1f}ms → {}",
                     copy_result->video_packets, copy_result->audio_packets,
                     copy_ms, opts.output.output);
        return 0;
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
    auto result = render_and_encode_ffmpeg_pipe(
        registry, compiled, composition_id,
        settings, start, end, render_opts, cpu_budget,
        std::move(video_runtimes),
        device_scheduler,
        std::move(execution));
    return result.return_code;
}

} // namespace chronon3d::cli
