namespace {

struct RenderPlanState {
    std::string input;
    std::string output;
    std::string assets_root;
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
    std::uint32_t fps_num{30};
    std::uint32_t fps_den{1};
    int crf{-1};
    std::string encode_preset;
    bool report{false};
    std::string backend{"auto"};
    std::shared_ptr<SoftwareRenderer> warm_renderer;
    RenderPlanVideoOverrides video;
    std::string trace_output;
    std::string trace_level{"pipeline"};
    std::string gpu_hot_path_mode{"auto"};
    std::shared_ptr<media::VideoJobExecutionContext> video_execution;
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
        request.video_settings.fps_num = args.fps_num == 30 && args.fps_den == 1
            ? prepared.canvas.fps.num() : args.fps_num;
        request.video_settings.fps_den = args.fps_num == 30 && args.fps_den == 1
            ? prepared.canvas.fps.den() : args.fps_den;
        request.video_settings.fps = static_cast<int>(std::lround(
            static_cast<double>(request.video_settings.fps_num) /
            request.video_settings.fps_den));
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
        // Preserve the optional source media path through the render-plan
        // adapter. The native encoder uses it to mux the master audio stream;
        // dropping it here produces a valid video-only MP4 silently.
        request.video_settings.gop_source = args.video.gop_source;
        request.video_settings.gop_copy_only = args.video.gop_copy_only;
        if (!args.video.ffmpeg_mode.empty())
            request.video_settings.ffmpeg_mode = args.video.ffmpeg_mode;
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
        // Explicitness markers: only CLI/API overrides and values that the
        // render plan itself decoded as explicit configuration count as user
        // requests. Engine placeholder defaults (rate_control default "crf"
        // with no crf/qp/bitrate value, the default encode preset) stay
        // unmarked so the encoder-configuration resolver can apply the
        // deterministic per-backend engine default instead of rejecting an
        // NVENC job that simply never mentioned rate control.
        request.video_settings.rate_control_mode_explicit =
            !args.video.rate_control_mode.empty() ||
            prepared.output.rate_control != render_plan::RateControlMode::Crf ||
            (prepared.output.crf > 0 && prepared.output.crf <= 51);
        request.video_settings.crf_explicit =
            args.video.crf >= 0 || args.crf >= 0 ||
            (prepared.output.crf > 0 && prepared.output.crf <= 51);
        request.video_settings.qp_explicit =
            args.video.qp >= 0 || prepared.output.qp >= 0;
        request.video_settings.bitrate_explicit =
            args.video.bitrate > 0 || prepared.output.bitrate > 0;
        request.video_settings.encode_preset_explicit =
            !args.video.encode_preset.empty() || !args.encode_preset.empty();
        request.video_settings.tune_explicit = false;
        if (video_output(output)) {
            request.mode = RenderMode::Video;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
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
        auto result = execute_render_job(
            *job, std::move(args.warm_renderer), std::move(args.video_execution));
        if (!result) {
            spdlog::error("Render plan job failed: {}", result.error().message);
            return 1;
        }
        if (args.report) {
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
        }
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Render plan error: {}", error.what());
        return 1;
    }
}

} // namespace
