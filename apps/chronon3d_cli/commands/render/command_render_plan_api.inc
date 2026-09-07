int run_render_plan_file(
    const CompositionRegistry& registry,
    runtime::job::RenderJobRequest request,
    std::shared_ptr<SoftwareRenderer> warm_renderer,
    std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
    const auto normalized = runtime::job::normalize_render_job(request);
    if (!normalized) {
        spdlog::error("Render job request normalization failed: {}",
                      normalized.error().message);
        return 1;
    }
    RenderPlanState state;
    state.normalized_request = std::move(normalized).value();
    state.warm_renderer = std::move(warm_renderer);
    state.video_execution = std::move(video_execution);
    return execute_render_plan(registry, state);
}

ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload) {
    return ipc_render_job(registry, payload, {});
}

ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload,
                          std::shared_ptr<SoftwareRenderer> warm_renderer,
                          std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
    try {
        const auto payload_json = nlohmann::json::parse(payload);
        runtime::job::RenderJobRequest request;
        request.plan_path = payload_json.value("plan_path", "");
        request.output_path = payload_json.value("output", "");
        request.assets_root = payload_json.value("assets_root", "");
        request.backend = payload_json.value("backend", "auto");
        request.report = payload_json.value("report", false);
        request.trace.output = payload_json.value("trace_output", "");
        request.trace.level = payload_json.value("trace_level", "pipeline");
        request.receipt_verification = payload_json.value("receipt_verify", "");

        const bool range_enabled = payload_json.value("range_enabled", false);
        const std::int64_t first_frame = payload_json.value(
            "first_frame", std::int64_t{0});
        const std::int64_t last_frame = payload_json.value(
            "last_frame", std::int64_t{0});
        if (range_enabled) {
            request.frame_range = runtime::job::FrameRange{first_frame, last_frame};
        }

        request.output.codec = payload_json.contains("output_spec")
            ? payload_json["output_spec"].value("codec", "")
            : payload_json.value("codec", "");
        request.output.hardware_encoder = payload_json.value("hardware_encoder", "");
        request.output.encoder_backend = payload_json.value("encoder_backend", "");
        request.output.ffmpeg_mode = payload_json.value("ffmpeg_mode", "");
        request.output.gpu_hot_path_mode = payload_json.value(
            "gpu_hot_path_mode", "");
        request.output.encode_preset = payload_json.value("encode_preset", "");
        request.output.rate_control_mode = payload_json.value("rate_control_mode", "");
        request.output.crf = payload_json.value("crf", -1);
        request.output.qp = payload_json.value("qp", -1);
        request.output.bitrate = payload_json.value("bitrate", std::int64_t{0});
        // Parity fields (see RenderingGen renderJobPayload / wave 1B): the
        // daemon contract carries the source audio stream (muxed through the
        // GOP source path) and the optional native encode preset exactly like
        // the CLI subprocess flags --gop-source / --encode-preset.
        request.output.audio_source_path = payload_json.value("audio_source_path", "");
        request.output.gop_copy_only = payload_json.value("gop_copy_only", false);

        if (payload_json.contains("execution_requirements")) {
            media::ExecutionRequirements exec_reqs;
            const auto& sr = payload_json["execution_requirements"];
            exec_reqs.gpu_required = sr.value("gpu_required", false);
            exec_reqs.cpu_fallback_allowed = sr.value("cpu_fallback_allowed", true);
            exec_reqs.composition_required = sr.value("composition_required", true);
            exec_reqs.packet_copy_allowed = sr.value("packet_copy_allowed", true);
            request.requirements = exec_reqs;
        } else {
            const std::string hw = payload_json.value(
                "hardware_encoder", payload_json.value("hardware", "none"));
            request.requirements.gpu_required = hw == "nvenc";
        }

        const auto output_path = request.output_path;
        const int rc = run_render_plan_file(
            registry, std::move(request), std::move(warm_renderer),
            std::move(video_execution));
        if (rc != 0) {
            return ipc::Reply{ipc::Status::Error,
                              "render job failed with exit code " + std::to_string(rc)};
        }

        nlohmann::json reply = {
            {"status", "ok"},
            {"output", output_path.string()},
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
    auto* start_frame_opt = command->add_option("--start-frame", state->start_frame, "First frame");
    auto* end_frame_opt = command->add_option("--end-frame", state->end_frame, "Last frame, inclusive");
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
    command->callback([state, &ctx, start_frame_opt, end_frame_opt] {
        if (!state->video_execution) {
            state->video_execution = std::make_shared<media::VideoJobExecutionContext>();
            state->video_execution->video_runtimes = ctx.video_runtimes;
        }
        // Explicitness comes from option PRESENCE: --start-frame 0 --end-frame 0
        // is a real single-frame chunk at frame 0; omitting both stays the
        // legacy whole-plan default.
        state->start_frame_explicit = start_frame_opt->count() > 0;
        state->end_frame_explicit = end_frame_opt->count() > 0;
        ctx.exit_code = run_render_plan_file(
            ctx.registry, make_render_job_request(*state),
            std::move(state->warm_renderer), std::move(state->video_execution));
    });
}
