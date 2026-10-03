#include "video_export_support.hpp"
#include "video_job_time_rescale.hpp"
#include "gop_smart_copy.hpp"
#include "encoder_config_resolution.hpp"
#include "encoder_bitstream_contract.hpp"
#include "gop_target_contract.hpp"
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
media::GopAnalysisSummary inspect_source_once(
    const FfmpegExportOptions& opts, const CompiledComposition& compiled,
    double start_sec, double end_sec,
    const chronon3d::CpuBudget& cpu_budget) {
    media::GopAnalysisSummary summary;
    if (opts.gop_source.empty()) return summary;
    const auto target = resolve_gop_target_contract(opts, compiled, cpu_budget);
    if (!target) {
        // No encoder-owned contract could be produced for a lane that
        // re-encodes: the splice gate cannot be evaluated, so it is unsafe.
        spdlog::warn(
            "[video] gop preflight: encoder-owned target contract unavailable; "
            "packet copy is not certified for this lane");
        summary.available = false;
        return summary;
    }
    const auto analysis = inspect_gop_source(
        opts.gop_source, *target,
        start_sec, end_sec,
        /*content_changed=*/!opts.gop_copy_only,
        opts.changed_intervals);
    if (!analysis) {
        summary.available = false;
        return summary;
    }
    spdlog::info(
        "[video] gop preflight target: producer={} materialized={}",
        target->producer == BitstreamTargetProducer::Remux ? "remux" : "encoder",
        target->encoder_materialized);
    summary.available = true;
    summary.total_gops = analysis->plans.size();
    summary.copy_gops = analysis->copy_count;
    summary.reencode_gops = analysis->reencode_count;
    summary.all_copy = analysis->all_copy_eligible;
    summary.hybrid = analysis->is_hybrid();
    summary.trim_only = analysis->trim_only;
    summary.source_time_base_num = analysis->source_time_base_num;
    summary.source_time_base_den = analysis->source_time_base_den;
    // Splice safety matters only when some GOPs are copied alongside
    // re-encoded ones: every planned Copy must have passed the gate.
    bool safe_to_splice = analysis->is_hybrid();
    if (safe_to_splice) {
        for (const auto& gop : analysis->plans) {
            if (gop.copy_packets() && !gop.compatibility.safe_to_splice()) {
                safe_to_splice = false;
                break;
            }
        }
    }
    summary.safe_to_splice = safe_to_splice;
    summary.work_units =
        build_gop_plan_from_analysis(*analysis, safe_to_splice).work_units;
    return summary;
}

// The analysis → schedule mapping is shared with the preflight and the parity
// gate: `build_gop_plan_from_analysis` (utils/video/gop_smart_copy.hpp). This
// lane uses it with `splice_safe = true`, which is exactly what declaring the
// container FROM the source certifies.

// ── The ONE hybrid executor: COPY + REENCODE → ONE MuxSession (P0-3/5E/5F) ──
//
// The declared architecture is:
//
//   GopExecutionPlan → GopExecutionExecutor → single caller-owned MuxSession
//                                           (COPY + REENCODE → one mux)
//
// This function IS that path. It used to exist twice: the legacy executor
// wrote one temp MP4 per segment and remuxed (encode→write→close→open→demux→
// timestamp-normalize→mux→delete), while GopExecutionExecutor only validated
// the schedule. P0-5G deleted the segment/file/remux path once the direct mux
// passed the parity gate (tests/cli/test_gop_direct_mux_parity.cpp).
//
// CONTAINER DECLARATION (P0-5D): the output container's video stream is
// declared from the compressed SOURCE, which makes a copied packet correct by
// construction. That declaration is also the only target this schedule is
// decided against — no encoder-owned contract exists yet at preflight time,
// which is exactly why the encoder owns the OTHER side of the comparison:
// every re-encoded work unit must REPRODUCE the declared stream and certifies
// it at close() (fail-closed). A source the encoder cannot match therefore
// never publishes a container a decoder cannot follow across the splice.
//
// P0-5F: audio is submitted exactly once and the container is finalized
// exactly once — here, after every work unit. A child render that finalized
// the borrowed container would close it after the FIRST re-encoded GOP and
// drop every later unit of the schedule.
bool execute_direct_mux_gop_plan(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    const std::shared_ptr<media::VideoJobExecutionContext>& execution,
    std::string& reason) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)registry; (void)compiled; (void)composition_id; (void)settings;
    (void)start; (void)end; (void)opts; (void)cpu_budget; (void)execution;
    reason = "native_ffmpeg_disabled";
    return false;
#else
    if (opts.gop_source.empty()) {
        reason = "no_compressed_source";
        return false;
    }
    if (!opts.variants.empty()) {
        // A SIMO fan-out encoder owns one container per variant; it cannot
        // share this caller-owned container. Refuse instead of silently
        // writing the variants' packets somewhere else.
        reason = "direct_mux_variants_unsupported";
        return false;
    }

    // ── 1. Declare the schedule against the stream the container WILL carry ─
    const auto declared = make_remux_target_contract(opts.gop_source);
    if (!declared) {
        reason = "source_contract_unavailable";
        return false;
    }
    // The borrowed mux lane must preserve the source bitstream profile for
    // copied GOPs. Velox assembly-ready assets have a stricter canonical
    // H.264 High contract, so a Main-profile source cannot enter this lane:
    // encoding only changed GOPs would keep the container at Main. Decline
    // the optimization and let the normal encoder produce one High stream
    // for the complete clip (audio can still be copied from gop_source).
    if (declared->codec == AV_CODEC_ID_H264 &&
        declared->profile != AV_PROFILE_H264_HIGH) {
        reason = "source_profile_not_assembly_ready";
        return false;
    }
    const double fps = opts.output.fps_value() > 0.0
        ? opts.output.fps_value() : 30.0;
    const double start_sec = static_cast<double>(start.integral()) / fps;
    const double end_sec = static_cast<double>(end.integral()) / fps;
    const auto analysis = inspect_gop_source(
        opts.gop_source, *declared, start_sec, end_sec,
        /*content_changed=*/!opts.changed_intervals.empty(),
        opts.changed_intervals);
    if (!analysis || !analysis->valid()) {
        reason = "gop_inspection_failed";
        return false;
    }
    if (!analysis->is_hybrid()) {
        // Whole-clip copy is the BitstreamCopy lane and a fully re-encoded
        // clip is the ordinary render path; neither belongs here.
        reason = analysis->all_copy_eligible ? "schedule_is_all_copy"
                                             : "schedule_has_no_copy_units";
        return false;
    }
    // The copied units are certified BY CONSTRUCTION on this lane: the
    // container was declared from the source, so a copied packet is the
    // output stream. The re-encoded units carry the other half of the proof —
    // each child encoder must reproduce that declaration (see
    // NativeAvEncoder::close).
    auto plan = build_gop_plan_from_analysis(*analysis, /*splice_safe=*/true);
    if (plan.work_units.empty()) {
        reason = "schedule_invalid";
        return false;
    }
    const std::int64_t source_tb_num = plan.source_time_base_num;
    const std::int64_t source_tb_den = plan.source_time_base_den;
    const auto rate = opts.output.frame_rate();

    // The requested window, in the schedule's own time base: exact integer
    // rescale of the frame window the job asked for.
    const std::int64_t request_start_pts = frame_to_tick(
        start, source_tb_num, source_tb_den, rate, FrameRounding::Floor);
    const std::int64_t request_end_pts = frame_to_tick(
        end, source_tb_num, source_tb_den, rate, FrameRounding::Ceiling);
    if (request_end_pts <= request_start_pts) {
        reason = "empty_request_window";
        return false;
    }

    // ── The units this window needs (the plan covers the whole source) ────
    const auto selected = media::gop_units_in_window(
        plan, request_start_pts, request_end_pts);
    if (selected.empty()) {
        reason = "window_outside_source";
        return false;
    }
    const auto& first_unit = plan.work_units[selected.front()];
    const auto& last_unit = plan.work_units[selected.back()];
    const std::int64_t first_unit_start = first_unit.render_range.present
        ? first_unit.render_range.first : first_unit.first_pts;
    const std::int64_t last_unit_start = last_unit.render_range.present
        ? last_unit.render_range.first : last_unit.first_pts;
    const std::int64_t last_unit_end = std::max<std::int64_t>(
        request_end_pts, last_unit_start + 1);
    // A COPIED unit is all-or-nothing: its packet envelope cannot be trimmed to
    // an arbitrary tick, so a window edge that falls INSIDE a copied unit is a
    // window this lane cannot honour. A re-encoded unit can be rendered for
    // exactly the requested frames, which is why the edges are only checked
    // against the units that are copied. Refusing (instead of emitting the
    // whole GOP) keeps the published frame count equal to the requested one.
    if (first_unit.copies_packets() && first_unit_start < request_start_pts) {
        reason = "window_start_not_gop_aligned";
        return false;
    }
    if (last_unit.copies_packets() && last_unit_end > request_end_pts) {
        reason = "window_end_not_gop_aligned";
        return false;
    }

    // Each unit's right edge: the next unit's start, clipped to the window for
    // the final one (a re-encode may stop mid-GOP; a copy never does).
    std::vector<std::int64_t> unit_end_ticks;
    unit_end_ticks.reserve(selected.size());
    for (std::size_t position = 0; position < selected.size(); ++position) {
        const auto& unit = plan.work_units[selected[position]];
        const std::int64_t unit_start = unit.render_range.present
            ? unit.render_range.first : unit.first_pts;
        const std::int64_t next_start =
            position + 1 < selected.size()
            ? (plan.work_units[selected[position + 1]].render_range.present
                   ? plan.work_units[selected[position + 1]].render_range.first
                   : plan.work_units[selected[position + 1]].first_pts)
            : request_end_pts;
        unit_end_ticks.push_back(std::max<std::int64_t>(
            next_start, unit_start + 1));
    }

    // ── The container's zero: the requested window's own first tick ───────
    // A trimmed export starts where the job asked it to, and every unit is
    // placed relative to that same integer tick — one interpretation of the
    // timeline, which the copy lane used to redo through seconds.
    const std::int64_t schedule_origin = request_start_pts;

    const auto final_path = std::filesystem::path(opts.output.output);
    const auto partial_path = make_partial_output_path(final_path);
    if (const auto parent = final_path.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            spdlog::error("[direct-mux] failed to create output directory '{}': {}",
                          parent.string(), ec.message());
            reason = "output_directory_unavailable";
            return false;
        }
    }

    // ── 2. ONE container for copied AND re-encoded work units (P0-5E) ──────
    media::MuxSession mux;
    const auto declaration = open_source_declared_mux(
        opts.gop_source, partial_path.string(), mux, /*include_audio=*/true);
    if (!declaration) {
        spdlog::error("[direct-mux] failed to declare the output container from '{}'",
                      opts.gop_source);
        reason = "container_declaration_failed";
        return false;
    }
    const auto discard_partial = [&]() {
        std::error_code ec;
        std::filesystem::remove(partial_path, ec);
    };

    // The executor consumes a plan whose units are exactly the selected ones,
    // in schedule order: the window decides WHICH units run, never what a unit
    // is. Ordinals are renumbered so a callback can index the schedule it was
    // handed (one authority for right edges, no ordinal remapping table).
    media::GopExecutionPlan window_plan = plan;
    window_plan.work_units.clear();
    window_plan.work_units.reserve(selected.size());
    for (const auto index : selected) {
        auto unit = plan.work_units[index];
        unit.ordinal = window_plan.work_units.size();
        window_plan.work_units.push_back(unit);
    }

    std::size_t copy_units = 0;
    std::size_t reencode_units = 0;
    media::GopExecutionCallbacks callbacks{
        .copy_packets = [&](const media::GopWorkUnit& unit,
                            media::MuxSession& session) {
            // Untouched GOPs enter the container verbatim, at their own
            // source timestamps: no second interpretation of the schedule.
            const auto submitted = submit_gop_source_packets(
                opts.gop_source, unit, session, schedule_origin);
            if (!submitted) {
                spdlog::error("[direct-mux] copy work unit {} failed", unit.ordinal);
                return false;
            }
            if (submitted->video_packets == 0) {
                spdlog::error(
                    "[direct-mux] copy work unit {} submitted no packets "
                    "(dts window {}-{})", unit.ordinal, unit.first_dts,
                    unit.last_dts);
                return false;
            }
            ++copy_units;
            return true;
        },
        .reencode = [&](const media::GopWorkUnit& unit,
                        media::MuxSession& session) {
            if (unit.ordinal >= unit_end_ticks.size()) {
                spdlog::error("[direct-mux] work unit {} has no scheduled right edge",
                              unit.ordinal);
                return false;
            }
            const std::int64_t unit_start = unit.render_range.present
                ? unit.render_range.first : unit.first_pts;
            // A unit that straddles the window's left edge contributes only the
            // frames inside the window (a re-encode can be rendered for exactly
            // the requested frames; the copy lane refuses such a unit above).
            const std::int64_t render_first =
                std::max<std::int64_t>(unit_start, schedule_origin);
            const Frame render_start = tick_to_frame(
                render_first, source_tb_num, source_tb_den, rate,
                FrameRounding::Floor);
            const Frame render_end = tick_to_frame(
                unit_end_ticks[unit.ordinal], source_tb_num, source_tb_den,
                rate, FrameRounding::Ceiling);
            if (render_end <= render_start) {
                spdlog::error(
                    "[direct-mux] work unit {} maps to an empty frame window "
                    "(ticks {}-{})", unit.ordinal, unit_start,
                    unit_end_ticks[unit.ordinal]);
                return false;
            }

            // P0-5E — this unit's output belongs at the tick the schedule
            // reserved for it, not at the container's zero. The child emits
            // its own zero-based ticks, so the placement is the unit's tick
            // RELATIVE to the window origin the copied units were shifted by.
            session.set_video_origin(render_first - schedule_origin);

            auto child = opts;
            child.output.output = opts.output.output;
            child.gop_copy_only = false;
            child.changed_intervals.clear();
            child.variants.clear();
            child.resolved_execution_reason = "direct_mux_work_unit";
            child.resolved_execution_valid = true;
            child.resolved_execution_downgrade_count = 0;
            child.resolved_execution_path =
                opts.resolved_execution_plan &&
                        opts.resolved_execution_plan->composite ==
                            media::CompositePath::DirectYuv
                    ? FfmpegExportOptions::ResolvedExecutionPath::DirectYuv
                    : FfmpegExportOptions::ResolvedExecutionPath::FullGraph;
            // The child writes into the caller's container and is certified
            // against the stream that container was declared from; it never
            // opens, finalizes or publishes a container of its own.
            child.borrowed_mux = BorrowedMuxTarget{&session, *declared};

            const auto rendered = render_and_encode_native(
                registry, compiled, composition_id, settings, render_start,
                render_end, child, cpu_budget,
                execution ? execution->video_runtimes : nullptr,
                execution ? execution->device_scheduler : nullptr,
                execution);
            if (rendered.return_code != 0 || !rendered.success) {
                spdlog::error(
                    "[direct-mux] re-encode work unit {} failed "
                    "(frames {}-{}, reason={})", unit.ordinal,
                    render_start.integral(), render_end.integral(),
                    child.resolved_execution_reason);
                return false;
            }
            ++reencode_units;
            return true;
        }};

    media::GopExecutionResult execution_result;
    std::string execution_reason;
    if (!media::GopExecutionExecutor::execute(window_plan, mux, callbacks,
                                              execution_result, execution_reason)) {
        spdlog::error("[direct-mux] schedule execution failed: {}", execution_reason);
        reason = "schedule_execution_failed";
        discard_partial();
        return false;
    }

    // ── 3. Audio once, trailer once, publish once (P0-5F) ────────────────
    // The audio window is the container's own window: from the schedule's left
    // edge to the right edge of its last work unit (the schedule, not the
    // requested frames, is authoritative for the last unit).
    const media::TimestampRange scheduled_window{
        schedule_origin, unit_end_ticks.back(), true};
    const auto audio = submit_source_audio(opts.gop_source, mux, scheduled_window,
                                           schedule_origin);
    if (!audio) {
        spdlog::error("[direct-mux] audio submission failed");
        reason = "audio_submission_failed";
        discard_partial();
        return false;
    }
    if (!mux.finalize()) {
        spdlog::error("[direct-mux] container finalize failed");
        reason = "container_finalize_failed";
        discard_partial();
        return false;
    }

    auto contract_result = media::video::resolve_output_contract("youtube_overlay_v1");
    if (!contract_result) {
        spdlog::error("[direct-mux] output contract unavailable: {}",
                      contract_result.error());
        reason = "output_contract_unavailable";
        discard_partial();
        return false;
    }
    auto contract = std::move(contract_result).value();
    contract.width = compiled.composition->width();
    contract.height = compiled.composition->height();
    contract.fps = rate;
    contract.frame_count = static_cast<std::int64_t>(end - start);
    contract.audio_required = mux.has_audio();
    contract.audio_streams = contract.audio_required ? 1 : 0;
    const auto verification =
        media::video::verify_output_contract(partial_path, contract);
    if (!verification.passed) {
        spdlog::error("[direct-mux] output failed verification: {}",
                      verification.failure);
        reason = "output_verification_failed";
        discard_partial();
        return false;
    }

    std::error_code rename_ec;
    std::filesystem::rename(partial_path, final_path, rename_ec);
    if (rename_ec) {
        spdlog::error("[direct-mux] failed to publish '{}': {}",
                      final_path.string(), rename_ec.message());
        reason = "publish_failed";
        discard_partial();
        return false;
    }
    spdlog::info(
        "[direct-mux] completed: {} copied work units, {} re-encoded work units, "
        "{} audio packets, sha256={} → {}",
        copy_units, reencode_units, audio->audio_packets,
        verification.sha256, final_path.string());
    reason = "direct_mux_hybrid";
    return true;
#endif
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
    std::shared_ptr<media::VideoJobExecutionContext> execution) {
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
        const media::GopAnalysisSummary summary = inspect_source_once(
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

        const auto final_path = std::filesystem::path(opts.output.output);
        // AUDIT §5 — the same staging path rule as the direct-mux lane.
        const auto partial_path = make_partial_output_path(final_path);

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
        if (execute_direct_mux_gop_plan(
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
        std::move(execution));
    return result.return_code;
}

} // namespace chronon3d::cli
