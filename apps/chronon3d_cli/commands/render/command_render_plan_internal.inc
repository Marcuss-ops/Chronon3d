namespace {

struct RenderPlanState {
    std::string input;
    std::string output;
    std::string assets_root;
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
    // start_frame/end_frame explicitness: only an EXPLICIT --start-frame /
    // --end-frame pair (or daemon range_enabled) makes an end value of 0 a
    // real inclusive last frame 0. When end was never requested, the legacy
    // end==0 sentinel still means "through the last frame".
    bool start_frame_explicit{false};
    bool end_frame_explicit{false};
    std::uint32_t fps_num{30};
    std::uint32_t fps_den{1};
    int crf{-1};
    std::string encode_preset;
    bool report{false};
    // Resolver-owned default (audit N8).
    std::string backend{std::string{media::kDefaultBackend}};
    std::shared_ptr<SoftwareRenderer> warm_renderer;
    RenderPlanVideoOverrides video;
    std::string trace_output;
    std::string trace_level{"pipeline"};
    std::string gpu_hot_path_mode{"auto"};
    std::string receipt_verify;
    std::shared_ptr<media::VideoJobExecutionContext> video_execution;
    std::optional<runtime::job::NormalizedRenderJob> normalized_request;
};

runtime::job::RenderJobRequest make_render_job_request(const RenderPlanState& args) {
    runtime::job::RenderJobRequest request;
    request.plan_path = args.input;
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
    request.output.encode_preset = !args.video.encode_preset.empty()
        ? args.video.encode_preset : args.encode_preset;
    request.output.rate_control_mode = args.video.rate_control_mode;
    request.output.crf = args.video.crf >= 0 ? args.video.crf : args.crf;
    request.output.qp = args.video.qp;
    request.output.bitrate = args.video.bitrate;
    request.output.audio_source_path = args.video.gop_source;
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

int execute_render_plan(const CompositionRegistry& registry, RenderPlanState& args) {
    try {
        // Phase 9 (CLI/IPC parity): normalization happens exactly once, at the
        // two canonical entry boundaries (run_render_plan_file / daemon IPC
        // decode). The execution layer never re-normalizes: every caller is
        // required to hand in an already-normalized job.
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
        auto preparation = prepare_render_plan(options);
        if (!preparation) {
            spdlog::error("Render plan preparation failed: {}",
                          preparation.error().message);
            return 1;
        }

        auto context = std::move(preparation).value();
        const auto& meta = context.program->metadata();
        const auto& fp = context.program->fingerprint_data();
        const auto& effective_assets_root = context.effective_assets_root;
        const auto& effective_settings = context.settings;

        const auto output = normalized_job.output_path.empty()
            ? meta.output.path : normalized_job.output_path.string();
        const auto first = normalized_job.frame_range
            ? normalized_job.frame_range->first : std::int64_t{0};
        // Explicit frame-range semantics: when --end-frame (or the daemon
        // range_end) was explicitly requested, an end value of 0 is a real
        // inclusive last frame 0 (single-frame chunk at frame 0). Without an
        // explicit end the legacy end==0 sentinel means "through the last
        // frame of the plan".
        const std::int64_t last = normalized_job.frame_range
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
        request.video_settings.codec = codec_name(static_cast<render_plan::VideoCodec>(meta.output.codec));
        request.video_settings.rate_control_mode = meta.output.rate_control == static_cast<std::uint8_t>(render_plan::RateControlMode::ConstantQp) ? "qp" :
            (meta.output.rate_control == static_cast<std::uint8_t>(render_plan::RateControlMode::Bitrate) ? "bitrate" : "crf");
        request.video_settings.qp = meta.output.qp;
        request.video_settings.bitrate = meta.output.bitrate;
        if (!normalized_job.output.codec.empty()) request.video_settings.codec = normalized_job.output.codec;
        if (!normalized_job.output.hardware_encoder.empty())
            request.video_settings.hardware_encoder = normalized_job.output.hardware_encoder;
        if (!normalized_job.output.encoder_backend.empty())
            request.video_settings.encoder_backend = normalized_job.output.encoder_backend;
        // Preserve the optional source media path through the render-plan
        // adapter. The native encoder uses it to mux the master audio stream;
        // dropping it here produces a valid video-only MP4 silently.
        request.video_settings.gop_source = normalized_job.output.audio_source_path;
        request.video_settings.gop_copy_only = normalized_job.output.gop_copy_only;
        request.video_settings.pipe_pixfmt = normalized_job.output.pipe_pixfmt;
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
        if (!normalized_job.output.encode_preset.empty()) {
            request.video_settings.encode_preset = normalized_job.output.encode_preset;
        }
        // Explicitness markers: only CLI/API overrides and values that the
        // render plan itself decoded as explicit configuration count as user
        // requests. Engine placeholder defaults (rate_control default "crf"
        // with no crf/qp/bitrate value, the default encode preset) stay
        // unmarked so the encoder-configuration resolver can apply the
        // deterministic per-backend engine default instead of rejecting an
        // NVENC job that simply never mentioned rate control.
        request.video_settings.rate_control_mode_explicit =
            !normalized_job.output.rate_control_mode.empty() ||
            meta.output.rate_control != static_cast<std::uint8_t>(render_plan::RateControlMode::Crf) ||
            (meta.output.crf > 0 && meta.output.crf <= 51);
        request.video_settings.crf_explicit =
            normalized_job.output.crf >= 0 ||
            (meta.output.crf > 0 && meta.output.crf <= 51);
        request.video_settings.qp_explicit =
            normalized_job.output.qp >= 0 || meta.output.qp >= 0;
        request.video_settings.bitrate_explicit =
            normalized_job.output.bitrate > 0 || meta.output.bitrate > 0;
        request.video_settings.encode_preset_explicit =
            !normalized_job.output.encode_preset.empty();
        request.video_settings.tune_explicit = false;
        // The (0,0) frame pair only re-expands to the whole plan when NO
        // range was requested. Explicit --start/--end or daemon range_enabled
        // keeps 0..0 as a real single-frame chunk (see RenderRequest docs).
        request.frame_range_explicit = normalized_job.frame_range.has_value();

        if (video_output(output)) {
            request.mode = RenderMode::Video;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
            const bool gpu_native =
                request.video_settings.hardware_encoder == "nvenc" &&
                request.video_settings.encoder_backend == "native" &&
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
        if (normalized_job.report) {
            try {
                verification::RenderReceiptInput receipt_input;
                receipt_input.job_id = meta.job_id;
                receipt_input.content_digest = fp.content_digest.hex();
                receipt_input.request_digest = fp.request_digest.hex();
                receipt_input.asset_manifest_digest =
                    meta.asset_manifest.manifest_digest().hex();
#ifdef CHRONON3D_CLI_PROJECT_VERSION
                receipt_input.chronon_version = CHRONON3D_CLI_PROJECT_VERSION;
#endif
                receipt_input.git_sha = telemetry::TelemetryManager::get_git_commit();
                receipt_input.backend = normalized_job.backend;
                receipt_input.width = meta.width;
                receipt_input.height = meta.height;
                receipt_input.fps_num = meta.fps_numerator;
                receipt_input.fps_den = meta.fps_denominator;
                receipt_input.frames = meta.duration_frames;
                receipt_input.requested_codec = codec_name(static_cast<render_plan::VideoCodec>(meta.output.codec));
                receipt_input.has_audio_tracks = false;
                // Explicit per-job verification policy (daemon RENDER_JOB
                // payload / render-plan boundary). Empty falls back to the
                // process environment (CHRONON_RECEIPT_VERIFY), preserving
                // the CLI subprocess contract.
                receipt_input.receipt_verify_policy = normalized_job.receipt_verification;

                const auto receipt = verification::build_render_receipt(
                    receipt_input, output, video_output(output));
                const auto receipt_path =
                    verification::write_render_receipt(receipt, output);
                spdlog::info("receipt: {}", receipt_path.string());
                spdlog::info(
                    "[video] receipt verification policy={} total_ms={:.1f} "
                    "sha256_ms={:.1f} probe_ms={:.1f} count_frames_ms={:.1f} "
                    "decode_ms={:.1f} copy_eligible={}",
                    receipt.verification.resolved_policy,
                    receipt.timing.total_ms, receipt.timing.sha256_ms,
                    receipt.timing.probe_ms, receipt.timing.count_frames_ms,
                    receipt.timing.decode_ms,
                    receipt.copy_eligible ? "true" : "false");
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

int execute_normalized_render_job(
    const CompositionRegistry& registry,
    runtime::job::NormalizedRenderJob job,
    std::shared_ptr<SoftwareRenderer> warm_renderer,
    std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
    RenderPlanState state;
    state.normalized_request = job;
    state.warm_renderer = std::move(warm_renderer);
    state.video_execution = std::move(video_execution);
    return execute_render_plan(registry, state);
}

} // namespace
