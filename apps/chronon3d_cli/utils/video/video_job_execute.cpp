#include "video_export_support.hpp"
#include "gop_smart_copy.hpp"
#include "../../commands/video/common/pipe_export_pipeline.hpp"
#include "../../commands/video/common/video_export_common.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/detail/video_execution_legacy.hpp>

#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <chrono>
#include <filesystem>
#include <utility>

namespace chronon3d::cli {

namespace {

// Build the target-side portion of the bitstream contract from the same
// export options that will be handed to the encoder.  Fields that are only
// known after encoder initialisation (profile/level/extradata) deliberately
// remain unknown; inspect_gop_source then fails closed until a concrete
// encoder contract is supplied.
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

    // Packet-copy output preserves the source codec parameters exactly. Use
    // those parameters as the target contract for the source-preserving mux;
    // this is the only valid target before a re-encoder has been opened. A
    // failed probe leaves the configured fields above and therefore remains
    // fail-closed in compare_bitstream_compatibility().
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

} // namespace

FfmpegExportOptions make_ffmpeg_export_options(const RenderJob& job) {
    OutputOptions output;
    output.output = job.output;
    output.frames_dir_name = job.video_settings.frames_dir;
    output.fps = job.video_settings.fps;
    output.fps_num = job.video_settings.fps_num;
    output.fps_den = job.video_settings.fps_den;

    EncoderOptions encoder;
    encoder.codec = job.video_settings.codec;
    encoder.hardware_encoder = job.video_settings.hardware_encoder;
    encoder.encode_preset = job.video_settings.encode_preset;
    encoder.tune = job.video_settings.tune;
    encoder.rate_control_mode = job.video_settings.rate_control_mode;
    encoder.crf = job.video_settings.crf;
    encoder.qp = job.video_settings.qp;
    encoder.bitrate = job.video_settings.bitrate;
    encoder.encoder_backend = job.video_settings.encoder_backend;

    PipeOptions pipe;
    pipe.pipe_pixfmt = job.video_settings.pipe_pixfmt;
    pipe.pipe_writer = job.video_settings.pipe_writer;
    pipe.color_output = job.video_settings.color_output;
    pipe.ffmpeg_verbose = job.video_settings.ffmpeg_verbose;

    RenderWarmupOptions warmup;
    warmup.warmup_renderer = job.execution.warmup_renderer;
    warmup.warmup_framebuffers = job.execution.warmup_framebuffers;
    warmup.warmup_dummy_frame = job.execution.warmup_dummy_frame;

    SinkOptions sink;
    sink.sink_type = VideoSinkType::Ffmpeg;
    sink.ffmpeg_mode = job.video_settings.ffmpeg_mode;
    sink.keep_frames = job.video_settings.keep_frames;
    sink.chunks = job.video_settings.chunks;

    FfmpegExportOptions opts;
    opts.output = std::move(output);
    opts.encoder = std::move(encoder);
    opts.pipe = std::move(pipe);
    opts.warmup = std::move(warmup);
    opts.sink = std::move(sink);
    opts.assets_root = job.execution.assets_root;
    opts.gop_source = job.video_settings.gop_source;
    opts.gop_copy_only = job.video_settings.gop_copy_only;
    // Honor the canonical backend preference resolved from --backend.
    // The video pipe exporter otherwise rebuilt a fresh Config::from_environment
    // (Auto → Software) and silently ignored --backend vulkan.
    opts.backend_preference = job.execution.config
        ? job.execution.config->backend_preference()
        : chronon3d::graph::BackendPreference::Auto;
    opts.gpu_hot_path_mode = job.execution.config
        ? job.execution.config->gpu_hot_path_mode()
        : chronon3d::GpuHotPathMode::Auto;
    return opts;
}

int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoJobExecutionContext> execution)
{
    if (opts.output.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (encoder_backend_requires_ffmpeg(opts.encoder.encoder_backend) &&
        !ffmpeg_in_path()) {
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

    // ── Whole-clip BitstreamCopy dispatch ──────────────────────────────
    // The canonical execution resolver decides the path before any renderer
    // or encoder is created. When it selects BitstreamCopy the clip has no
    // visual modifications (no watermark, no subtitles, no background) and
    // the source codec is compatible: the compressed bitstream is copied
    // packet-by-packet directly into the output container. NVDEC, CUDA
    // compositing and NVENC are never initialised — zero GPU work.
    //
    // This dispatch MUST stay above the pipe exporter call. The pipe
    // exporter's setup_pipe_export_session() also calls the resolver but
    // treats BitstreamCopy as an error ("bitstream-copy execution must use
    // the packet pipeline") because the pipe renderer cannot do packet
    // copy. Routing here ensures copy_gop_source() is the single entry.
    if (opts.sink.ffmpeg_mode == "pipe") {
        const auto path_decision = media::detail::resolve_legacy_video_execution(
            media::detail::LegacyVideoExecutionRequest{
                .encoder_backend = opts.encoder.encoder_backend,
                .hardware_encoder = opts.encoder.hardware_encoder,
                .codec = opts.encoder.codec,
                .hot_path = opts.gpu_hot_path_mode,
                .has_gop_source = !opts.gop_source.empty(),
                .gop_copy_only = opts.gop_copy_only,
                .allow_hybrid_gop = !opts.gop_source.empty() &&
                    !opts.gop_copy_only});
        if (path_decision.path == media::VideoExecutionPath::BitstreamCopy &&
            path_decision.valid) {
            // Explicit whole-clip copy is only an intent. Validate the
            // source before creating any renderer or encoder. The packet
            // preflight is fail-closed: an unavailable/incompatible source
            // never gets copied; it is routed to DirectYuv instead.
            const double fps_val = opts.output.fps_value();
            const double start_sec = static_cast<double>(start.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);
            const double end_sec = static_cast<double>(end.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);
            const auto analysis = inspect_gop_source(
                opts.gop_source, make_bitstream_target_contract(opts, compiled,
                                                                opts.gop_source), start_sec, end_sec);
            if (!analysis || !analysis->all_copy_eligible) {
                spdlog::warn("[bitstream-copy] compatibility preflight failed; "
                             "falling back to DirectYuv before renderer setup");
                // Re-enter the render path with an explicit DirectYuv
                // decision. Do not retry BitstreamCopy and do not let the
                // lower-level exporter infer a path.
                auto fallback_opts = opts;
                fallback_opts.gop_copy_only = false;
                fallback_opts.gop_source.clear();
                fallback_opts.resolved_execution_path =
                    FfmpegExportOptions::ResolvedExecutionPath::DirectYuv;
                fallback_opts.resolved_execution_plan = media::detail::resolve_legacy_video_execution(
                    media::detail::LegacyVideoExecutionRequest{
                        .encoder_backend = opts.encoder.encoder_backend,
                        .hardware_encoder = opts.encoder.hardware_encoder,
                        .codec = opts.encoder.codec,
                        .hot_path = opts.gpu_hot_path_mode}).plan;
                auto fallback = render_and_encode_ffmpeg_pipe(
                    registry, compiled, composition_id, settings,
                    start, end, fallback_opts, cpu_budget,
                    nullptr, nullptr, std::move(execution));
                return fallback.return_code;
            }
            spdlog::info("[bitstream-copy] path selected (reason: {}); "
                         "bypassing NVDEC/CUDA/NVENC entirely",
                         path_decision.reason);
            const auto copy_t0 = profiling::now();

            // The source timeline uses its own time_base; copy_gop_source
            // converts internally. A frame range [start, end) maps to
            // [start/fps, end/fps) in seconds.

            // Ensure the output directory exists.
            if (const auto parent = std::filesystem::path(opts.output.output)
                    .parent_path(); !parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    spdlog::error("[bitstream-copy] failed to create output "
                                  "directory '{}': {}",
                                  parent.string(), ec.message());
                    return 1;
                }
            }

            // Atomic output: write to .partial then rename on success.
            auto final_path = std::filesystem::path(opts.output.output);
            auto partial_path = final_path;
            const auto ext = final_path.extension();
            if (ext.empty()) {
                partial_path += ".partial";
            } else {
                partial_path.replace_filename(
                    final_path.stem().string() + ".partial" + ext.string());
            }

            auto copy_result = copy_gop_source(
                opts.gop_source,
                partial_path.string(),
                start_sec, end_sec);
            const auto copy_t1 = profiling::now();
            const double copy_ms = profiling::duration_ms(copy_t0, copy_t1);

            if (!copy_result) {
                spdlog::error("[bitstream-copy] copy_gop_source failed for "
                              "source '{}' → '{}'",
                              opts.gop_source, partial_path.string());
                std::error_code rm_ec;
                std::filesystem::remove(partial_path, rm_ec);
                return 1;
            }

            // Atomic rename: .partial → final.
            std::error_code rename_ec;
            std::filesystem::rename(partial_path, final_path, rename_ec);
            if (rename_ec) {
                spdlog::error("[bitstream-copy] failed to rename '{}' → '{}': {}",
                              partial_path.string(), final_path.string(),
                              rename_ec.message());
                return 1;
            }

            spdlog::info("[bitstream-copy] completed: {} video packets, "
                         "{} audio packets in {:.1f}ms → {}",
                         copy_result->video_packets,
                         copy_result->audio_packets,
                         copy_ms, opts.output.output);
            return 0;  // success — no render/encode needed
        }

        // ── Smart GOP hybrid dispatch ──────────────────────────────────
        // The resolver selected SmartGopCopy: the source has a compressed
        // bitstream and some GOPs may be eligible for packet copy while
        // others need DirectYUV/NVENC re-encode. The plan is produced by
        // inspect_gop_source() at execution time.
        //
        // Current implementation: inspect the source to determine if ALL
        // GOPs are copy-eligible (whole-clip copy fast path) or if the
        // plan is truly hybrid. When hybrid, the safe_to_splice gate
        // must pass; otherwise we downgrade to DirectYuv for the entire
        // clip (the pipe exporter handles it).
        if (path_decision.path == media::VideoExecutionPath::SmartGopCopy &&
            path_decision.valid) {
            const double fps_val = opts.output.fps_value();
            const double start_sec = static_cast<double>(start.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);
            const double end_sec = static_cast<double>(end.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);

            spdlog::info("[smart-gop] inspecting source '{}' [{:.2f}s, {:.2f}s)",
                         opts.gop_source, start_sec, end_sec);
            auto analysis = inspect_gop_source(
                opts.gop_source, make_bitstream_target_contract(opts, compiled,
                                                                opts.gop_source),
                start_sec, end_sec);

            if (analysis && analysis->all_copy_eligible) {
                // All GOPs are clean and codec-compatible → whole-clip
                // copy, zero GPU. Same fast path as BitstreamCopy.
                spdlog::info("[smart-gop] all {} GOPs copy-eligible; "
                             "routing to whole-clip copy",
                             analysis->plans.size());
                auto final_path = std::filesystem::path(opts.output.output);
                auto partial_path = final_path;
                const auto ext = final_path.extension();
                if (ext.empty()) {
                    partial_path += ".partial";
                } else {
                    partial_path.replace_filename(
                        final_path.stem().string() + ".partial" + ext.string());
                }
                if (const auto parent = final_path.parent_path(); !parent.empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(parent, ec);
                }
                const auto copy_t0 = profiling::now();
                auto copy_result = copy_gop_source(
                    opts.gop_source, partial_path.string(),
                    start_sec, end_sec);
                const double copy_ms = profiling::duration_ms(copy_t0, profiling::now());
                if (!copy_result) {
                    std::error_code rm_ec;
                    std::filesystem::remove(partial_path, rm_ec);
                    spdlog::error("[smart-gop] copy_gop_source failed");
                    return 1;
                }
                std::error_code rename_ec;
                std::filesystem::rename(partial_path, final_path, rename_ec);
                if (rename_ec) {
                    spdlog::error("[smart-gop] rename failed: {}",
                                  rename_ec.message());
                    return 1;
                }
                spdlog::info("[smart-gop] whole-clip copy: {} video packets, "
                             "{} audio packets in {:.1f}ms",
                             copy_result->video_packets,
                             copy_result->audio_packets, copy_ms);
                return 0;
            }

            if (analysis && analysis->is_hybrid()) {
                // True hybrid: some GOPs copy, others re-encode. This
                // requires the unified PacketAssembler pipeline that can
                // accept both copied and NVENC-encoded packets.
                //
                // FAIL_CLOSED: the BitstreamCompatibility gate must pass
                // for every copy-eligible GOP. If any GOP's gate fails
                // (codec/profile/level/SPS/PPS mismatch), splicing source
                // H.264 + NVENC H.264 would corrupt the decoder (green
                // frames, seek errors, player-specific crashes). We
                // downgrade to DirectYuv for the ENTIRE clip — correct
                // but not optimal. The full hybrid pipeline (per-GOP demux
                // + selective NVDEC/NVENC + PacketAssembler merge) is a
                // future step; until then the safe fallback guarantees a
                // valid output.
                bool all_safe = true;
                for (const auto& plan : analysis->plans) {
                    if (plan.copy_packets() &&
                        !plan.compatibility.safe_to_splice()) {
                        all_safe = false;
                        spdlog::warn("[smart-gop] GOP [{}, {}] copy-eligible "
                                     "but BitstreamCompatibility gate FAILED "
                                     "— splicing source+NVENC would corrupt",
                                     plan.first_pts, plan.last_pts);
                        break;
                    }
                }
                if (!all_safe) {
                    spdlog::info("[smart-gop] BitstreamCompatibility gate "
                                 "failed; downgrading to DirectYuv for the "
                                 "entire clip (fail-closed)");
                    // Fall through to the pipe exporter (DirectYuv).
                } else {
                    spdlog::info("[smart-gop] hybrid plan: {}/{} GOPs copy, "
                                 "{}/{} re-encode; splice_safe=true; "
                                 "downgrading to DirectYuv "
                                 "(hybrid pipeline TBD)",
                                 analysis->copy_count, analysis->plans.size(),
                                 analysis->reencode_count,
                                 analysis->plans.size());
                    // Fall through to the pipe exporter (DirectYuv path).
                }
            } else {
                // No GOPs are copy-eligible (all touched or codec
                // mismatch). DirectYuv for the entire clip is the
                // correct path.
                spdlog::info("[smart-gop] no copy-eligible GOPs; "
                             "routing to DirectYuv");
                // Fall through to the pipe exporter.
            }
        }

        // The resolver has already dispatched all packet-copy paths above.
        // Only DirectYuv and FullGraph are allowed to enter the renderer
        // session builder; it must never resolve the path again.
        auto render_opts = opts;
        const auto render_path =
            path_decision.path == media::VideoExecutionPath::DirectYuv
                ? media::VideoExecutionPath::DirectYuv
                : path_decision.render_fallback;
        render_opts.resolved_execution_path =
            render_path == media::VideoExecutionPath::DirectYuv
                ? FfmpegExportOptions::ResolvedExecutionPath::DirectYuv
                : FfmpegExportOptions::ResolvedExecutionPath::FullGraph;
        render_opts.resolved_execution_plan = path_decision.plan;
        auto result = render_and_encode_ffmpeg_pipe(
            registry, compiled, composition_id,
            settings, start, end, render_opts, cpu_budget,
            nullptr, nullptr, std::move(execution));
        return result.return_code;
    }
    return 1;
}

} // namespace chronon3d::cli
