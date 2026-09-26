#include "command_render_plan_internal.hpp"

#include "../../utils/job/render_job.hpp"
#include "../../utils/job/output_lane_pixel_truth.hpp"
#include "../video/common/pipe_pixfmt_vocabulary.hpp"
#include "../video/common/sink_options.hpp"
#include "../../utils/video/encoder_config_resolution.hpp"
#include "../../utils/video/copy_lane_coverage.hpp"
#include "render_plan_preparation.hpp"

#include <chronon3d/render_plan/audio_mix_compiler.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#ifdef CHRONON3D_ENABLE_VERIFICATION
#include <chronon3d/verification/render_receipt.hpp>
#endif
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <atomic>
#include <cerrno>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <string>

namespace chronon3d::cli {

runtime::job::RenderJobRequest make_render_job_request(const RenderPlanState& args) {
    runtime::job::RenderJobRequest request;
    request.plan_path = args.input;
    request.prepared_package_path = args.prepared_package;
    request.output_path = args.output;
    request.assets_root = args.assets_root;
    request.report = args.report;
    request.backend = args.backend;
    request.trace.output = args.trace_output;
    request.trace.level = args.trace_level;
    request.receipt_verification = args.receipt_verify;
    request.output.codec = args.video.codec;
    request.output.hardware_encoder = args.video.hardware_encoder;
    request.output.encoder_backend = args.video.encoder_backend;
    request.output.ffmpeg_mode = args.video.ffmpeg_mode;
    request.output.gpu_hot_path_mode = args.gpu_hot_path_mode;
    if (!args.video_sink.empty()) request.output.sink_type = args.video_sink;
    request.output.encode_preset = !args.video.encode_preset.empty()
        ? args.video.encode_preset : args.encode_preset;
    request.output.rate_control_mode = args.video.rate_control_mode;
    request.output.crf = args.video.crf >= 0 ? args.video.crf : args.crf;
    request.output.qp = args.video.qp;
    request.output.bitrate = args.video.bitrate;
    request.output.gop_source = args.video.gop_source;
    request.output.audio_source_path = args.video.gop_source;
    request.output.audio_target_sample_rate = args.video.audio_sample_rate;
    request.output.gop_copy_only = args.video.gop_copy_only;
    request.output.pipe_pixfmt = args.video.pipe_pixfmt;
    request.output.fps_num = args.fps_num == 30 && args.fps_den == 1
        ? 0U : args.fps_num;
    request.output.fps_den = args.fps_den;
    if (args.start_frame_explicit || args.end_frame_explicit) {
        request.frame_range = runtime::job::FrameRange{
            static_cast<std::int64_t>(args.start_frame),
            static_cast<std::int64_t>(args.end_frame)};
    }
    return request;
}

namespace {

graph::BackendPreference backend_preference_from_name(const std::string& value) {
    if (value == "software") return graph::BackendPreference::Software;
    if (value == "vulkan") return graph::BackendPreference::GPU;
    return graph::BackendPreference::Auto;
}

std::string codec_name(render_plan::VideoCodec codec) {
    switch (codec) {
        case render_plan::VideoCodec::H264: return "h264";
        case render_plan::VideoCodec::H265: return "h265";
        case render_plan::VideoCodec::VP9: return "vp9";
        case render_plan::VideoCodec::AV1: return "av1";
        case render_plan::VideoCodec::Auto: return "auto";
    }
    return "auto";
}

bool video_output(const std::string& path) {
    const auto extension = std::filesystem::path(path).extension().string();
    return extension == ".mp4" || extension == ".mkv" || extension == ".webm" || extension == ".gif" ||
           extension == ".mov";
}

int run_ffmpeg_child(const std::vector<std::string>& arguments) {
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back("ffmpeg");
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
#if defined(_WIN32)
    const intptr_t status = _spawnvp(_P_WAIT, "ffmpeg", argv.data());
    return status < 0 ? 127 : static_cast<int>(status);
#else
    const pid_t child = fork();
    if (child < 0) return 127;
    if (child == 0) {
        execvp("ffmpeg", const_cast<char* const*>(argv.data()));
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 127;
#endif
}

struct TemporaryAudioMix {
    std::filesystem::path path;
    ~TemporaryAudioMix() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};

std::optional<std::string> premix_plan_audio(
    const render_plan::RenderPlan& plan,
    const chronon3d::assets::AssetResolver& resolver,
    TemporaryAudioMix& temporary,
    int sample_rate) {
    if (!plan.audio) return std::nullopt;
    const auto compiled = render_plan::AudioMixCompiler::compile(
        *plan.audio, plan.canvas, resolver, sample_rate);
    if (!compiled) {
        const auto& error = compiled.error();
        return error.code + ": " + error.path + ": " + error.message;
    }
    static std::atomic<std::uint64_t> sequence{0};
    temporary.path = std::filesystem::temp_directory_path() /
        ("chronon-audio-mix-" + std::to_string(sequence.fetch_add(1)) + ".m4a");
    std::error_code ignored;
    std::filesystem::remove(temporary.path, ignored);
    std::vector<std::string> args{"-hide_banner", "-v", "error", "-y"};
    for (const auto& input : compiled->inputs) {
        args.emplace_back("-i");
        args.push_back(input.path);
    }
    args.emplace_back("-filter_complex");
    args.push_back(compiled->filter_complex);
    args.emplace_back("-map");
    args.emplace_back("[out]");
    args.emplace_back("-c:a");
    args.emplace_back("aac");
    args.emplace_back("-b:a");
    args.emplace_back("192k");
    args.emplace_back("-ar");
    args.push_back(std::to_string(sample_rate));
    args.push_back(temporary.path.string());
    const int status = run_ffmpeg_child(args);
    if (status != 0 || !std::filesystem::is_regular_file(temporary.path)) {
        std::filesystem::remove(temporary.path, ignored);
        temporary.path.clear();
        return status == 127
            ? std::optional<std::string>{"audio_ffmpeg_executable_missing"}
            : std::optional<std::string>{"audio_ffmpeg_premix_failed"};
    }
    return std::nullopt;
}

} // namespace

int execute_render_plan(const CompositionRegistry& registry, RenderPlanState& args) {
    try {
        if (!args.normalized_request) {
            spdlog::error(
                "execute_render_plan requires a NormalizedRenderJob (Phase 9: "
                "normalization happens exactly once at the CLI/IPC boundary)");
            return 1;
        }
        const auto& normalized_job = *args.normalized_request;

        RenderPlanPreparationOptions options;
        options.input = normalized_job.plan_path.string();
        options.assets_root = normalized_job.assets_root.string();
        options.prepared_package = normalized_job.prepared_package_path.string();
        auto preparation = prepare_render_plan(options);
        if (!preparation) {
            spdlog::error("Render plan preparation failed: {}",
                          preparation.error().message);
            return 1;
        }
        auto context = std::move(preparation).value();

        if (context.decoded.audio &&
            (!normalized_job.output.gop_source.empty() ||
             !normalized_job.output.audio_source_path.empty())) {
            spdlog::error("render-plan audio cannot be combined with --gop-source or a legacy audio source");
            return 1;
        }

        TemporaryAudioMix temporary_audio_mix;
        std::string audio_source_path = normalized_job.output.audio_source_path;

        const auto coverage = validate_copy_lane_coverage(
            context.decoded, normalized_job.output.gop_copy_only,
            normalized_job.output.changed_intervals);
        if (!coverage.ok) {
            spdlog::error("[video] refusing copy-lane job: {}", coverage.message);
            return 1;
        }

        const auto& meta = context.program->metadata();
        const auto& fp = context.program->fingerprint_data();
        const auto& effective_assets_root = context.effective_assets_root;
        const auto& effective_settings = context.settings;
        auto output = normalized_job.output_path.empty()
            ? meta.output.path : normalized_job.output_path.string();
        if (context.decoded.output.format == render_plan::OutputFormat::Gif &&
            std::filesystem::path(output).extension() != ".gif") {
            std::filesystem::path gif_path(output);
            gif_path.replace_extension(".gif");
            output = gif_path.string();
        }
        if (context.decoded.output.format == render_plan::OutputFormat::PngSequence &&
            output.find("####") == std::string::npos) {
            const auto extension = output.find_last_of('.');
            output.insert(extension == std::string::npos ? output.size() : extension, "_####");
        }
        if (context.decoded.audio) {
            if (!is_native_encoder_transport(normalized_job.output.encoder_backend)) {
                spdlog::error("[audio-plan] render-plan audio requires the native FFmpeg encoder");
                return 1;
            }
            if ((!video_output(output) &&
                 !video_sink_name_selects_video_lane(normalized_job.output.sink_type)) ||
                normalized_job.output.sink_type == "raw") {
                spdlog::error("[audio-plan] render-plan audio requires an encoded video output");
                return 1;
            }
            const int mix_sample_rate = normalized_job.output.audio_target_sample_rate > 0
                ? normalized_job.output.audio_target_sample_rate : 48000;
            if (const auto error = premix_plan_audio(
                    context.decoded, context.resolver, temporary_audio_mix,
                    mix_sample_rate)) {
                spdlog::error("[audio-plan] {}", *error);
                return 1;
            }
            audio_source_path = temporary_audio_mix.path.string();
        }
        const auto first = normalized_job.frame_range
            ? normalized_job.frame_range->first : std::int64_t{0};
        const auto last = normalized_job.frame_range
            ? normalized_job.frame_range->last
            : static_cast<std::int64_t>(meta.duration_frames) - 1;

        RenderRequest request;
        request.comp_id = meta.job_id;
        request.compiled_program = context.program;
        request.compiled_composition = context.video_compatibility_view;
        request.output = output;
        request.execution.assets_root = effective_assets_root.empty()
            ? std::nullopt
            : std::optional<std::filesystem::path>{effective_assets_root};
        request.settings = effective_settings;
        Config renderer_config = Config::from_environment();
        renderer_config.set_backend_preference(
            backend_preference_from_name(normalized_job.backend));
        if (!normalized_job.output.gpu_hot_path_mode.empty()) {
            renderer_config.set_gpu_hot_path_mode(
                parse_gpu_hot_path_mode(normalized_job.output.gpu_hot_path_mode));
        }
        request.execution.config = std::move(renderer_config);
        request.execution.report = normalized_job.report;
        request.execution.trace_output = normalized_job.trace.output;
        request.execution.trace_level = normalized_job.trace.level;
        request.video_settings.fps_num = normalized_job.output.fps_num == 0
            ? meta.fps_numerator : normalized_job.output.fps_num;
        request.video_settings.fps_den = normalized_job.output.fps_num == 0
            ? meta.fps_denominator : normalized_job.output.fps_den;
        request.video_settings.codec = codec_name(
            static_cast<render_plan::VideoCodec>(meta.output.codec));
        if (context.decoded.output.format == render_plan::OutputFormat::Gif)
            request.video_settings.codec = "gif";
        request.video_settings.rate_control_mode =
            meta.output.rate_control == static_cast<std::uint8_t>(render_plan::RateControlMode::ConstantQp)
                ? "qp"
                : (meta.output.rate_control == static_cast<std::uint8_t>(render_plan::RateControlMode::Bitrate)
                    ? "bitrate" : "crf");
        request.video_settings.qp = meta.output.qp;
        request.video_settings.bitrate = meta.output.bitrate;
        if (!normalized_job.output.codec.empty()) request.video_settings.codec = normalized_job.output.codec;
        if (context.decoded.output.format == render_plan::OutputFormat::Gif)
            request.video_settings.codec = "gif";
        if (!normalized_job.output.hardware_encoder.empty())
            request.video_settings.hardware_encoder = normalized_job.output.hardware_encoder;
        if (!normalized_job.output.encoder_backend.empty())
            request.video_settings.encoder_backend = normalized_job.output.encoder_backend;
        request.video_settings.gop_source = !normalized_job.output.gop_source.empty()
            ? normalized_job.output.gop_source
            : normalized_job.output.audio_source_path;
        request.video_settings.audio_source_path = audio_source_path;
        request.video_settings.audio_target_sample_rate =
            normalized_job.output.audio_target_sample_rate;
        request.video_settings.gop_copy_only = normalized_job.output.gop_copy_only;
        request.video_settings.changed_intervals = normalized_job.output.changed_intervals;
        request.video_settings.pipe_pixfmt = normalized_job.output.pipe_pixfmt;
        if (!normalized_job.output.sink_type.empty()) {
            request.video_settings.sink_type = normalized_job.output.sink_type;
            request.video_settings.sink_type_explicit = true;
        }
        if (!normalized_job.output.ffmpeg_mode.empty())
            request.video_settings.ffmpeg_mode = normalized_job.output.ffmpeg_mode;
        if (!normalized_job.output.rate_control_mode.empty()) {
            request.video_settings.rate_control_mode = normalized_job.output.rate_control_mode;
            request.video_settings.crf = normalized_job.output.crf;
            request.video_settings.qp = normalized_job.output.qp;
            request.video_settings.bitrate = normalized_job.output.bitrate;
        } else if (normalized_job.output.crf >= 0) {
            request.video_settings.rate_control_mode = "crf";
            request.video_settings.crf = normalized_job.output.crf;
        } else if (meta.output.crf > 0 && meta.output.crf <= 51) {
            request.video_settings.rate_control_mode = "crf";
            request.video_settings.crf = meta.output.crf;
        }
        if (!normalized_job.output.encode_preset.empty())
            request.video_settings.encode_preset = normalized_job.output.encode_preset;
        request.video_settings.rate_control_mode_explicit =
            !normalized_job.output.rate_control_mode.empty() ||
            meta.output.rate_control != static_cast<std::uint8_t>(render_plan::RateControlMode::Crf) ||
            (meta.output.crf > 0 && meta.output.crf <= 51);
        request.video_settings.crf_explicit = normalized_job.output.crf >= 0 ||
            (meta.output.crf > 0 && meta.output.crf <= 51);
        request.video_settings.qp_explicit = normalized_job.output.qp >= 0 || meta.output.qp >= 0;
        request.video_settings.bitrate_explicit = normalized_job.output.bitrate > 0 || meta.output.bitrate > 0;
        request.video_settings.encode_preset_explicit = !normalized_job.output.encode_preset.empty();
        request.video_settings.tune_explicit = false;
        request.frame_range_explicit = normalized_job.frame_range.has_value();

        if (video_output(output) ||
            video_sink_name_selects_video_lane(normalized_job.output.sink_type)) {
            request.mode = RenderMode::Video;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
            const bool gpu_native =
                hardware_nvenc_requested(request.video_settings.hardware_encoder) &&
                is_native_encoder_transport(request.video_settings.encoder_backend) &&
                normalized_job.backend == "vulkan";
            request.execution.warmup_renderer = !gpu_native;
            request.execution.warmup_dummy_frame = !gpu_native;
        } else if (first == last) {
            request.mode = RenderMode::Still;
            request.still_frame = Frame{first};
        } else {
            request.mode = RenderMode::Sequence;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
        }
        pixel_truth::announce_lane(request.mode);

        auto job = resolve_render_request(registry, std::move(request));
        if (!job) {
            spdlog::error("Render plan job failed: {}", job.error().message);
            return 1;
        }
        const auto lifecycle_execute_t0 = std::chrono::steady_clock::now();
        auto result = execute_render_job(
            *job, std::move(args.warm_renderer), std::move(args.video_execution));
        const auto lifecycle_execute_t1 = std::chrono::steady_clock::now();
        if (!result) {
            spdlog::error("Render plan job failed: {}", result.error().message);
            return 1;
        }
        double lifecycle_receipt_build_ms = 0.0;
        double lifecycle_receipt_write_ms = 0.0;
        if (normalized_job.report) {
#ifdef CHRONON3D_ENABLE_VERIFICATION
            try {
                verification::RenderReceiptInput receipt_input;
                receipt_input.job_id = meta.job_id;
                receipt_input.content_digest = fp.content_digest.hex();
                receipt_input.request_digest = fp.request_digest.hex();
                receipt_input.asset_manifest_digest = meta.asset_manifest.manifest_digest().hex();
#ifdef CHRONON3D_CLI_PROJECT_VERSION
                receipt_input.chronon_version = CHRONON3D_CLI_PROJECT_VERSION;
#endif
                receipt_input.git_sha = telemetry::TelemetryManager::get_git_commit();
                receipt_input.backend = normalized_job.backend;
                receipt_input.render_lane = result->render_lane.empty() ? normalized_job.backend : result->render_lane;
                receipt_input.lane_reason = result->lane_reason;
                receipt_input.width = meta.width;
                receipt_input.height = meta.height;
                receipt_input.fps_num = meta.fps_numerator;
                receipt_input.fps_den = meta.fps_denominator;
                receipt_input.frames = meta.duration_frames;
                receipt_input.requested_codec = codec_name(
                    static_cast<render_plan::VideoCodec>(meta.output.codec));
                receipt_input.has_audio_tracks = false;
                receipt_input.receipt_verify_policy = normalized_job.receipt_verification;
                const auto receipt_build_t0 = std::chrono::steady_clock::now();
                const auto receipt = verification::build_render_receipt(
                    receipt_input, output, video_output(output));
                const auto receipt_build_t1 = std::chrono::steady_clock::now();
                const auto receipt_path = verification::write_render_receipt(receipt, output);
                const auto receipt_write_t1 = std::chrono::steady_clock::now();
                lifecycle_receipt_build_ms = std::chrono::duration<double, std::milli>(
                    receipt_build_t1 - receipt_build_t0).count();
                lifecycle_receipt_write_ms = std::chrono::duration<double, std::milli>(
                    receipt_write_t1 - receipt_build_t1).count();
                spdlog::info("receipt: {}", receipt_path.string());
                spdlog::info(
                    "[video] receipt verification policy={} total_ms={:.1f} sha256_ms={:.1f} "
                    "probe_ms={:.1f} count_frames_ms={:.1f} decode_ms={:.1f} copy_eligible={}",
                    receipt.verification.resolved_policy, receipt.timing.total_ms,
                    receipt.timing.sha256_ms, receipt.timing.probe_ms,
                    receipt.timing.count_frames_ms, receipt.timing.decode_ms,
                    receipt.copy_eligible ? "true" : "false");
            } catch (const std::exception& error) {
                spdlog::warn("receipt generation failed: {}", error.what());
            }
#else
            spdlog::info(
                "render report requested but verification/receipts are disabled in this build");
#endif
        }
        spdlog::info(
            "[INFO] job-lifecycle: execute_ms={:.1f} receipt_build_ms={:.1f} "
            "receipt_write_ms={:.1f} caller_tail_ms={:.1f}",
            std::chrono::duration<double, std::milli>(
                lifecycle_execute_t1 - lifecycle_execute_t0).count(),
            lifecycle_receipt_build_ms, lifecycle_receipt_write_ms,
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - lifecycle_execute_t1).count());
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Render plan error: {}", error.what());
        return 1;
    }
}

int execute_normalized_render_job(
    const CompositionRegistry& registry,
    runtime::job::NormalizedRenderJob job,
    std::shared_ptr<SoftwareRenderer> warm_renderer,
    std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
    RenderPlanState state;
    state.normalized_request = std::move(job);
    state.warm_renderer = std::move(warm_renderer);
    state.video_execution = std::move(video_execution);
    return execute_render_plan(registry, state);
}

} // namespace chronon3d::cli
