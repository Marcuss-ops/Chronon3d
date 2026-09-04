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

    const auto path_decision = media::detail::resolve_legacy_video_execution(
        media::detail::LegacyVideoExecutionRequest{
            .encoder_backend = opts.encoder.encoder_backend,
            .hardware_encoder = opts.encoder.hardware_encoder,
            .codec = opts.encoder.codec,
            .hot_path = opts.gpu_hot_path_mode,
            .has_gop_source = !opts.gop_source.empty(),
            .gop_copy_only = opts.gop_copy_only,
            .allow_hybrid_gop = !opts.gop_source.empty() && !opts.gop_copy_only});

    if (path_decision.path == media::VideoExecutionPath::BitstreamCopy && path_decision.valid) {
        const double fps_val = opts.output.fps_value();
        const double start_sec = static_cast<double>(start.integral()) /
            (fps_val > 0.0 ? fps_val : 30.0);
        const double end_sec = static_cast<double>(end.integral()) /
            (fps_val > 0.0 ? fps_val : 30.0);
        const auto analysis = inspect_gop_source(
            opts.gop_source, make_bitstream_target_contract(opts, compiled, opts.gop_source),
            start_sec, end_sec);
        if (!analysis || !analysis->all_copy_eligible) {
            spdlog::warn("[bitstream-copy] compatibility preflight failed; falling back to DirectYuv before renderer setup");
            auto fallback_opts = opts;
            fallback_opts.gop_copy_only = false;
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

        spdlog::info("[bitstream-copy] path selected (reason: {}); bypassing NVDEC/CUDA/NVENC entirely",
                     path_decision.reason);
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

    if (path_decision.path == media::VideoExecutionPath::SmartGopCopy && path_decision.valid) {
        const double fps_val = opts.output.fps_value();
        const double start_sec = static_cast<double>(start.integral()) /
            (fps_val > 0.0 ? fps_val : 30.0);
        const double end_sec = static_cast<double>(end.integral()) /
            (fps_val > 0.0 ? fps_val : 30.0);
        spdlog::info("[smart-gop] inspecting source '{}' [{:.2f}s, {:.2f}s)",
                     opts.gop_source, start_sec, end_sec);
        auto analysis = inspect_gop_source(
            opts.gop_source, make_bitstream_target_contract(opts, compiled, opts.gop_source),
            start_sec, end_sec);

        if (analysis && analysis->all_copy_eligible) {
            spdlog::info("[smart-gop] all {} GOPs copy-eligible; routing to whole-clip copy",
                         analysis->plans.size());
            auto final_path = std::filesystem::path(opts.output.output);
            auto partial_path = final_path;
            const auto ext = final_path.extension();
            if (ext.empty()) partial_path += ".partial";
            else partial_path.replace_filename(
                final_path.stem().string() + ".partial" + ext.string());
            if (const auto parent = final_path.parent_path(); !parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }
            const auto copy_t0 = profiling::now();
            auto copy_result = copy_gop_source(
                opts.gop_source, partial_path.string(), start_sec, end_sec);
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
                spdlog::error("[smart-gop] rename failed: {}", rename_ec.message());
                return 1;
            }
            spdlog::info("[smart-gop] whole-clip copy: {} video packets, {} audio packets in {:.1f}ms",
                         copy_result->video_packets, copy_result->audio_packets, copy_ms);
            return 0;
        }

        if (analysis && analysis->is_hybrid()) {
            bool all_safe = true;
            for (const auto& plan : analysis->plans) {
                if (plan.copy_packets() && !plan.compatibility.safe_to_splice()) {
                    all_safe = false;
                    spdlog::warn("[smart-gop] GOP [{}, {}] copy-eligible but BitstreamCompatibility gate FAILED — splicing source+NVENC would corrupt",
                                 plan.first_pts, plan.last_pts);
                    break;
                }
            }
            if (!all_safe) {
                spdlog::info("[smart-gop] BitstreamCompatibility gate failed; downgrading to DirectYuv for the entire clip (fail-closed)");
            } else {
                spdlog::info("[smart-gop] hybrid plan: {}/{} GOPs copy, {}/{} re-encode; splice_safe=true; downgrading to DirectYuv (hybrid pipeline TBD)",
                             analysis->copy_count, analysis->plans.size(),
                             analysis->reencode_count, analysis->plans.size());
            }
        } else {
            spdlog::info("[smart-gop] no copy-eligible GOPs; routing to DirectYuv");
        }
    }

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

} // namespace chronon3d::cli
