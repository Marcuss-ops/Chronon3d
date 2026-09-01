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
                         const std::string& gpu_hot_path_mode,
                         std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
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
        const auto request = nlohmann::json::parse(payload);
        const std::string plan_path = request.value("plan_path", "");
        const std::string assets_root = request.value("assets_root", "");
        const std::string output = request.value("output", "");
        std::string backend = request.value("backend", "auto");
        std::string gpu_hot_path_mode = request.value(
            "gpu_hot_path_mode", "auto");
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

        if (request.contains("execution_requirements")) {
            media::ExecutionRequirements exec_reqs;
            const auto& sr = request["execution_requirements"];
            exec_reqs.gpu_required = sr.value("gpu_required", false);
            exec_reqs.cpu_fallback_allowed = sr.value("cpu_fallback_allowed", true);
            exec_reqs.composition_required = sr.value("composition_required", true);
            exec_reqs.packet_copy_allowed = sr.value("packet_copy_allowed", true);
            const std::string req_codec = request.contains("output_spec")
                ? request["output_spec"].value("codec", "")
                : request.value("codec", "");
            const auto canon = media::resolve_canonical_execution_parameters(
                exec_reqs, req_codec, backend, "auto");
            if (backend == "auto") backend = canon.backend;
            if (video.hardware_encoder.empty()) video.hardware_encoder = canon.hardware_encoder;
            if (video.encoder_backend.empty()) video.encoder_backend = canon.encoder_backend;
            if (gpu_hot_path_mode == "auto") gpu_hot_path_mode = canon.gpu_hot_path_mode;
            if (video.codec.empty()) video.codec = canon.codec;
        }
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
                                            /*trace_level=*/"pipeline",
                                            gpu_hot_path_mode,
                                            std::move(video_execution));
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
