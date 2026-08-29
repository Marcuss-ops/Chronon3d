#include "../../command_registry.hpp"
#include "../../cli_context.hpp"
#include "../../utils/job/render_job.hpp"
#include "command_render_plan.hpp"
#include "render_plan_preparation.hpp"

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/verification/render_receipt.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace chronon3d::cli {
namespace {

struct RenderPlanState {
    std::string input;
    std::string output;
    std::string assets_root;
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
    std::uint32_t fps_num{30};
    std::uint32_t fps_den{1};
    // Optional encoder overrides. Negative / empty = keep the engine default
    // (production balance: crf 20, preset medium — see render_job.hpp).
    int         crf{-1};
    std::string encode_preset;
    bool report{false};
    std::string backend{"auto"};
    std::shared_ptr<SoftwareRenderer> warm_renderer;
    RenderPlanVideoOverrides video;
    // Timeline tracing (--trace): .pftrace output path; empty = off.
    std::string trace_output;
    // Trace capture level: pipeline | nodes | full.
    std::string trace_level{"pipeline"};
    std::string gpu_hot_path_mode{"auto"};
};

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
    return extension == ".mp4" || extension == ".mkv" || extension == ".webm" ||
           extension == ".mov";
}

int execute_render_plan(const CompositionRegistry& registry, const RenderPlanState& args) {
    try {
        RenderPlanPreparationOptions options;
        options.input = args.input;
        options.assets_root = args.assets_root;
        auto preparation = prepare_render_plan(options);
        if (!preparation) {
            spdlog::error("Render plan preparation failed: {}",
                          preparation.error().message);
            return 1;
        }

        auto context = std::move(preparation).value();
        const auto& prepared = context.prepared;
        const auto& effective_assets_root = context.effective_assets_root;
        const auto& effective_settings = context.settings;

        const auto output = args.output.empty() ? prepared.output.path : args.output;
        const auto first = static_cast<std::int64_t>(args.start_frame);
        const auto last = args.end_frame == 0
            ? prepared.canvas.duration.integral() - 1
            : static_cast<std::int64_t>(args.end_frame);
        RenderRequest request;
        request.comp_id = prepared.job_id;
        request.compiled_composition = std::make_shared<const CompiledComposition>(
            prepared.compiled_composition);
        request.output = output;
        request.execution.assets_root = effective_assets_root.empty()
            ? std::nullopt
            : std::optional<std::filesystem::path>{effective_assets_root};
        request.settings = effective_settings;
        Config renderer_config = Config::from_environment();
        renderer_config.set_backend_preference(
            backend_preference_from_name(args.backend));
        if (!args.gpu_hot_path_mode.empty()) {
            renderer_config.set_gpu_hot_path_mode(
                parse_gpu_hot_path_mode(args.gpu_hot_path_mode));
        }
        request.execution.config = std::move(renderer_config);
        request.execution.report = args.report;
        request.execution.trace_output = std::filesystem::path(args.trace_output);
        request.execution.trace_level = args.trace_level;
        request.video_settings.fps = args.fps_num == 30 && args.fps_den == 1
            ? static_cast<int>(std::lround(prepared.canvas.fps.fps()))
            : static_cast<int>(args.fps_num / args.fps_den);
        request.video_settings.codec = codec_name(prepared.output.codec);
        request.video_settings.rate_control_mode = prepared.output.rate_control == render_plan::RateControlMode::ConstantQp ? "qp" :
            (prepared.output.rate_control == render_plan::RateControlMode::Bitrate ? "bitrate" : "crf");
        request.video_settings.qp = prepared.output.qp;
        request.video_settings.bitrate = prepared.output.bitrate;
        if (!args.video.codec.empty()) request.video_settings.codec = args.video.codec;
        if (!args.video.hardware_encoder.empty())
            request.video_settings.hardware_encoder = args.video.hardware_encoder;
        if (!args.video.encoder_backend.empty())
            request.video_settings.encoder_backend = args.video.encoder_backend;
        if (!args.video.ffmpeg_mode.empty())
            request.video_settings.ffmpeg_mode = args.video.ffmpeg_mode;
        // Rate-control overrides are explicit; legacy CRF remains accepted
        // only as the CRF-mode value at this boundary.
        if (!args.video.rate_control_mode.empty()) {
            request.video_settings.rate_control_mode = args.video.rate_control_mode;
            request.video_settings.crf = args.video.crf;
            request.video_settings.qp = args.video.qp;
            request.video_settings.bitrate = args.video.bitrate;
        } else if (args.crf >= 0) {
            request.video_settings.rate_control_mode = "crf";
            request.video_settings.crf = args.crf;
        } else if (prepared.output.crf > 0 && prepared.output.crf <= 51) {
            request.video_settings.rate_control_mode = "crf";
            request.video_settings.crf = prepared.output.crf;
        }
        if (!args.video.encode_preset.empty()) {
            request.video_settings.encode_preset = args.video.encode_preset;
        } else if (!args.encode_preset.empty()) {
            request.video_settings.encode_preset = args.encode_preset;
        }
        if (video_output(output)) {
            request.mode = RenderMode::Video;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
            // A CPU warmup frame must not populate the graph/framebuffer cache
            // before the Vulkan+NVENC video loop requests native surfaces.
            const bool gpu_native =
                request.video_settings.hardware_encoder == "nvenc" &&
                request.video_settings.encoder_backend == "native" &&
                args.backend == "vulkan";
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

        auto job = resolve_render_request(registry, std::move(request));
        if (!job) {
            spdlog::error("Render plan job failed: {}", job.error().message);
            return 1;
        }
        auto result = execute_render_job(*job, std::move(args.warm_renderer));
        if (!result) {
            spdlog::error("Render plan job failed: {}", result.error().message);
            return 1;
        }
        // M6 — canonical render receipt, emitted after a successful render so
        // downstream tooling can verify content identity + copy_eligible.
        try {
            verification::RenderReceiptInput receipt_input;
            receipt_input.job_id = prepared.job_id;
            receipt_input.content_digest = prepared.fingerprint.content_digest.hex();
            receipt_input.request_digest = prepared.fingerprint.request_digest.hex();
            receipt_input.asset_manifest_digest =
                prepared.assets.manifest_digest().hex();
#ifdef CHRONON3D_CLI_PROJECT_VERSION
            receipt_input.chronon_version = CHRONON3D_CLI_PROJECT_VERSION;
#endif
            receipt_input.git_sha = telemetry::TelemetryManager::get_git_commit();
            // Preserve the backend selected for this render in the canonical
            // receipt.  The previous default (software) made a successful
            // Vulkan/NVENC artifact look like a CPU render to downstream
            // certification tools.
            receipt_input.backend = args.backend;
            receipt_input.width = prepared.canvas.width;
            receipt_input.height = prepared.canvas.height;
            receipt_input.fps_num = prepared.canvas.fps.num();
            receipt_input.fps_den = prepared.canvas.fps.den();
            receipt_input.frames = prepared.canvas.duration.integral();
            receipt_input.requested_codec = codec_name(prepared.output.codec);
            receipt_input.has_audio_tracks = false;

            const auto receipt = verification::build_render_receipt(
                receipt_input, output, video_output(output));
            const auto receipt_path =
                verification::write_render_receipt(receipt, output);
            spdlog::info("receipt: {}", receipt_path.string());
        } catch (const std::exception& error) {
            spdlog::warn("receipt generation failed: {}", error.what());
        }
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Render plan error: {}", error.what());
        return 1;
    }
}

}  // namespace

int run_render_plan_file(const CompositionRegistry& registry,
                         const std::string& input,
                         const std::string& output,
                         const std::string& assets_root,
                         bool report,
                         std::shared_ptr<SoftwareRenderer> warm_renderer,
                         const std::string& backend,
                         RenderPlanVideoOverrides video,
                         const std::string& trace_output,
                         const std::string& trace_level,
                         const std::string& gpu_hot_path_mode) {
    RenderPlanState state;
    state.input = input;
    state.output = output;
    state.assets_root = assets_root;
    state.report = report;
    state.backend = backend;
    state.warm_renderer = std::move(warm_renderer);
    state.video = std::move(video);
    state.trace_output = trace_output;
    state.trace_level = trace_level;
    state.gpu_hot_path_mode = gpu_hot_path_mode;
    return execute_render_plan(registry, state);
}

ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload) {
    return ipc_render_job(registry, payload, {});
}

ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload,
                          std::shared_ptr<SoftwareRenderer> warm_renderer) {
    try {
        const auto request = nlohmann::json::parse(payload);
        const std::string plan_path = request.value("plan_path", "");
        const std::string assets_root = request.value("assets_root", "");
        const std::string output = request.value("output", "");
        const std::string backend = request.value("backend", "auto");
        const bool report = request.value("report", false);
        RenderPlanVideoOverrides video;
        video.codec = request.value("codec", "");
        video.hardware_encoder = request.value("hardware_encoder", "");
        video.encoder_backend = request.value("encoder_backend", "");
        video.ffmpeg_mode = request.value("ffmpeg_mode", "");
        video.encode_preset = request.value("encode_preset", "");
        video.rate_control_mode = request.value("rate_control_mode", "");
        video.crf = request.value("crf", -1);
        video.qp = request.value("qp", -1);
        video.bitrate = request.value("bitrate", std::int64_t{0});
        if (plan_path.empty()) {
            return ipc::Reply{ipc::Status::BadRequest,
                              "RENDER_JOB requires a plan_path"};
        }
        if (backend != "auto" && backend != "software" && backend != "vulkan") {
            return ipc::Reply{ipc::Status::BadRequest,
                              "RENDER_JOB backend must be auto, software, or vulkan"};
        }

        const int rc = run_render_plan_file(registry, plan_path, output, assets_root,
                                            report, std::move(warm_renderer), backend,
                                            std::move(video), /*trace_output=*/"",
                                            /*trace_level=*/"pipeline");
        if (rc != 0) {
            return ipc::Reply{ipc::Status::Error,
                              "render job failed with exit code " + std::to_string(rc)};
        }

        nlohmann::json reply = {
            {"status", "ok"},
            {"output", output},
        };
        return ipc::Reply{ipc::Status::Ok, reply.dump()};
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::BadRequest,
                          std::string{"RENDER_JOB parse failed: "} + e.what()};
    }
}

void register_render_plan_command(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "render-plan", "Compatibility alias for render --plan");
    auto state = std::make_shared<RenderPlanState>();
    command->add_option("--input", state->input, "Render plan JSON")->required();
    command->add_option("--output", state->output, "Output file (overrides plan output.path)");
    command->add_option("--assets-root", state->assets_root, "Instance asset root");
    command->add_option("--backend", state->backend,
                        "Render backend: auto, software, or vulkan")
        ->check(CLI::IsMember({"auto", "software", "vulkan"}));
    command->add_option("--start-frame", state->start_frame, "First frame");
    command->add_option("--end-frame", state->end_frame, "Last frame, inclusive");
    command->add_option("--fps-num", state->fps_num, "Frame-rate numerator");
    command->add_option("--fps-den", state->fps_den, "Frame-rate denominator");
    command->add_option("--rate-control", state->video.rate_control_mode, "Rate control: crf, qp, or bitrate")
        ->check(CLI::IsMember({"crf", "qp", "bitrate"}));
    command->add_option("--qp", state->video.qp, "Encoder QP override (0-63)");
    command->add_option("--bitrate", state->video.bitrate, "Encoder bitrate override in bits/second");
    command->add_option("--crf", state->crf, "Encoder CRF override (0-51; default = engine default)");
    command->add_option("--encode-preset", state->encode_preset, "x264 preset override (e.g. medium, veryfast)");
    command->add_option("--trace", state->trace_output,
                        "Write a Perfetto timeline trace to this .pftrace path (job-end only, RING_BUFFER)");
    command->add_option("--trace-level", state->trace_level,
                        "Trace capture level: pipeline (default), nodes, or full")
        ->check(CLI::IsMember({"pipeline", "nodes", "full"}));
    command->add_option("--gpu-hot-path-mode", state->gpu_hot_path_mode,
                        "GPU hot-path mode: auto, require_gpu_native, or require_direct_yuv")
        ->check(CLI::IsMember({"auto", "require_gpu_native", "require_direct_yuv"}));
    command->callback([state, &ctx] { ctx.exit_code = execute_render_plan(ctx.registry, *state); });
}

}  // namespace chronon3d::cli
