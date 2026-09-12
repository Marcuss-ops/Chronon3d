int run_render_plan_file(
    const CompositionRegistry& registry,
    runtime::job::RenderJobRequest request,
    std::shared_ptr<SoftwareRenderer> warm_renderer,
    std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
    // CLI boundary: parse happened at the flag layer, normalize happens here
    // — exactly once per job, exactly like the IPC boundary.
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
                          const runtime::job::NormalizedRenderJob& job,
                          std::shared_ptr<SoftwareRenderer> warm_renderer,
                          std::shared_ptr<media::VideoJobExecutionContext> video_execution) {
    // Phase 9 (CLI/IPC parity): the job arrives already normalized from the
    // daemon boundary. This function only executes; there is no JSON parse
    // and no re-normalization anywhere in the render execution layer.
    const auto output_path = job.output_path;
    const int rc = execute_normalized_render_job(
        registry, job, std::move(warm_renderer), std::move(video_execution));
    if (rc != 0) {
        return ipc::Reply{ipc::Status::Error,
                          "render job failed with exit code " + std::to_string(rc)};
    }

    nlohmann::json reply = {
        {"status", "ok"},
        {"output", output_path.string()},
    };
    return ipc::Reply{ipc::Status::Ok, reply.dump()};
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
    command->add_option("--pipe-pixfmt", state->video.pipe_pixfmt,
                        "CUDA/pipe surface format: nv12 or p010")
        ->check(CLI::IsMember({"nv12", "p010"}));
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
