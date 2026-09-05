void register_render_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    render_job_dispatcher() = [registry = &ctx.registry](
        const std::string& payload,
        std::shared_ptr<media::VideoJobExecutionContext> video_execution) -> ipc::Reply {
        return ipc_render_job(*registry, payload, {}, std::move(video_execution));
    };
    warm_render_job_dispatcher() = [registry = &ctx.registry](
        const std::string& payload,
        std::shared_ptr<SoftwareRenderer> renderer,
        std::shared_ptr<media::VideoJobExecutionContext> video_execution) -> ipc::Reply {
        return ipc_render_job(*registry, payload, std::move(renderer), std::move(video_execution));
    };

    auto* cmd = app.add_subcommand("render", "Render a composition");
    cmd->add_option("input", args.comp_id, "Composition name");
    cmd->add_option("--plan", state->plan_file, "Render a chronon.render-plan.v1 file through the canonical executor");
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("--frame", args.frames, "Single frame number (alias for --frames)");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_option("--assets-root", args.assets_root, "Root directory for resolving relative assets");
    cmd->add_option("--backend", state->backend, "Render backend: auto, software, or vulkan (strict)")
        ->default_val("auto")->check(CLI::IsMember({"auto", "software", "vulkan"}));
    cmd->add_option("--props-file", state->props_file, "Flat JSON object containing composition props");
    cmd->add_option("--props-json", state->props_json, "Inline flat JSON object containing composition props (mutually exclusive with --props-file)");
    cmd->add_option("--profile", state->profile, "Render profile: draft | preview | production | maximum")
        ->default_val("production")
        ->transform([](std::string value) { return lower_profile(std::move(value)); })
        ->check(CLI::IsMember(std::set<std::string>{"draft", "preview", "production", "maximum"}, CLI::ignore_case));
    cmd->add_option("-v,--log-level", args.log_level, "Log level: trace | debug | info | warn | error");
    cmd->add_option("--trace", args.trace_output, "Write a Perfetto timeline trace to this .pftrace path (job-end only, RING_BUFFER)");
    cmd->add_option("--trace-level", args.trace_level, "Trace capture level: pipeline (default), nodes, or full")
        ->default_val("pipeline")->check(CLI::IsMember({"pipeline", "nodes", "full"}));
    cmd->add_option("--gpu-hot-path-mode", state->gpu_hot_path_mode, "GPU hot-path mode: auto, require_gpu_native, or require_direct_yuv")
        ->default_val("auto")->check(CLI::IsMember({"auto", "require_gpu_native", "require_direct_yuv"}));

    add_video_options(*cmd, args);

    auto* advanced = cmd->add_option_group("Advanced", "Low-level renderer, memory, cache and diagnostic controls");
    auto* diagnostic = advanced->add_flag("--diagnostic,--layout-preview", args.pipeline.diagnostic, "Enable layout preview overlays");
    advanced->add_flag("--diagnostic-plan", args.pipeline.diagnostic_plan, "Log graph preflight diagnostics before each frame");
    advanced->add_option("--diagnostic-plan-output", args.pipeline.diagnostic_plan_output, "Write graph preflight report to a path pattern");
    auto* no_dirty_rects = advanced->add_flag("--no-dirty-rects", args.pipeline.no_dirty_rects, "Disable dirty-rectangle invalidation");
    auto* tile_size = advanced->add_option("--tile-size", args.pipeline.tile_size, "Tile size for dirty-rect execution");
    auto* motion_blur = advanced->add_flag("--motion-blur", args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    auto* motion_blur_mode = advanced->add_option("--motion-blur-mode", state->motion_blur_mode, "Motion blur mode: 0=off, 1=temporal, 2=velocity")
        ->check(CLI::Range(0, 2));
    auto* motion_blur_samples = advanced->add_option("--motion-blur-samples", args.pipeline.quality.motion_blur_samples, "Subframe samples");
    advanced->add_option("--shutter-angle", args.pipeline.quality.shutter_angle_deg, "Shutter angle in degrees");
    advanced->add_option("--shutter-phase", args.pipeline.quality.shutter_phase_deg, "Shutter phase in degrees");
    advanced->add_option("--motion-blur-pattern", state->motion_blur_pattern, "Sample pattern: 0=Uniform, 1=Stratified, 2=Halton")
        ->check(CLI::Range(0, 2));
    advanced->add_option("--motion-blur-filter", state->motion_blur_filter, "Reconstruction filter: 0=Box, 1=Triangle, 2=Gaussian")
        ->check(CLI::Range(0, 2));
    advanced->add_option("--ssaa", args.pipeline.quality.ssaa, "Super-sampling factor");
    advanced->add_flag("--benchmark-all", args.benchmark_all, "Write detailed graph-node phase durations");
    advanced->add_flag("--report", args.report, "Generate an execution report log");
    advanced->add_flag("--debug-text-layout", args.pipeline.text_layout_debug, "Draw text layout debug overlay and emit structured logs");
    advanced->add_flag("--diagnostic-overlay", args.pipeline.diagnostic_overlay, "Draw bbox, anchor and baseline overlays");
    advanced->add_flag("--diagnostic-overlay-only", args.pipeline.diagnostic_overlay_only, "Draw diagnostic overlay on a transparent background");
    advanced->add_option("--debug-text-layout-json", args.pipeline.text_layout_debug_json_path, "Write per-TextRun bounds JSON");
    advanced->add_flag("--force-scalar-normal-blend", args.pipeline.force_scalar_normal_blend, "Force scalar Normal blend for regression diagnosis");
    advanced->add_flag("--warmup-renderer", args.pipeline.warmup_renderer, "Preallocate framebuffers and prime caches");
    auto* warmup_framebuffers = advanced->add_option("--warmup-framebuffers", args.pipeline.warmup_framebuffers, "Number of framebuffers to preallocate");
    advanced->add_flag("--warmup-dummy-frame", args.pipeline.warmup_dummy_frame, "Render a dummy frame to prime caches");
    advanced->add_option("--fb-pool-budget-mb", args.pipeline.fb_pool_budget_mb, "Framebuffer pool retention budget in MB");
    advanced->add_option("--fb-pool-clear-policy", state->fb_pool_clear_policy, "keep-warm | trim-after-job | trim-on-memory-pressure")
        ->check(CLI::IsMember({"keep-warm", "trim-after-job", "trim-on-memory-pressure"}));
    advanced->add_option("--program-cache-capacity", args.pipeline.program_cache_capacity, "SceneProgramCache entries per Precomp node");
    auto* program_cache_tune = advanced->add_flag("--program-cache-tune", args.pipeline.program_cache_tune, "Auto-tune SceneProgramCache capacity");
    advanced->add_option("--program-cache-tune-interval", args.pipeline.program_cache_tune_interval, "Frames between auto-tune checks");
    advanced->add_option("--program-cache-tune-min", args.pipeline.program_cache_tune_min_capacity, "Minimum capacity when down-tuning");
    advanced->add_option("--program-cache-tune-max", args.pipeline.program_cache_tune_max_capacity, "Maximum capacity when up-tuning");

    cmd->add_flag_callback("--help-advanced", [] { print_advanced_render_help(std::cout); throw CLI::Success(); }, "Print advanced render options and exit");
    cmd->allow_windows_style_options();
    cmd->callback([
        cmd, state, &ctx, tile_size, no_dirty_rects, motion_blur,
        motion_blur_mode, motion_blur_samples, warmup_framebuffers,
        diagnostic, program_cache_tune
    ]() {
        auto& render_args = *state->args;
        commit_typed_runtime_options(*state);
        mark_video_option_explicitness(*cmd, render_args);
        if (auto* pipe_pixfmt = cmd->get_option("--pipe-pixfmt")) {
            render_args.pipe_pixfmt_explicit = pipe_pixfmt->count() > 0;
        }
        if (!state->plan_file.empty()) {
            if (render_args.video_settings.dry_run) {
                RenderPlanPreparationOptions options;
                options.input = state->plan_file;
                options.assets_root = render_args.assets_root;
                ctx.exit_code = validate_render_plan(options);
                return;
            }
            ctx.exit_code = run_render_plan_file(
                ctx.registry, state->plan_file, render_args.output, render_args.assets_root,
                render_args.report, {}, backend_cli_name(render_args.backend),
                RenderPlanVideoOverrides{
                    .codec = render_args.video_settings.codec,
                    .hardware_encoder = render_args.video_settings.hardware_encoder,
                    .encoder_backend = render_args.video_settings.encoder_backend,
                    .ffmpeg_mode = render_args.video_settings.ffmpeg_mode,
                    // Only options explicitly requested on the command line
                    // override the render plan: the engine placeholder
                    // defaults (rate_control_mode="crf", crf, preset, ...)
                    // must not be mistaken for explicit user requests when
                    // the encoder-configuration resolver runs.
                    .encode_preset = render_args.video_settings.encode_preset_explicit
                        ? render_args.video_settings.encode_preset : std::string{},
                    .rate_control_mode = render_args.video_settings.rate_control_mode_explicit
                        ? render_args.video_settings.rate_control_mode : std::string{},
                    .crf = render_args.video_settings.crf_explicit
                        ? render_args.video_settings.crf : -1,
                    .qp = render_args.video_settings.qp_explicit
                        ? render_args.video_settings.qp : -1,
                    .bitrate = render_args.video_settings.bitrate_explicit
                        ? render_args.video_settings.bitrate : std::int64_t{0},
                    .gop_source = render_args.video_settings.gop_source,
                    .gop_copy_only = render_args.video_settings.gop_copy_only},
                render_args.trace_output, render_args.trace_level,
                std::string(chronon3d::to_string(render_args.gpu_hot_path_mode)),
                [&ctx] {
                    auto execution = std::make_shared<media::VideoJobExecutionContext>();
                    execution->video_runtimes = ctx.video_runtimes;
                    return execution;
                }());
            return;
        }
        if (render_args.comp_id.empty()) {
            spdlog::error("render requires a composition name or --plan <file>");
            ctx.exit_code = 1;
            return;
        }
        const auto explicitly_set = [&](std::string_view flag) {
            if (flag == "--tile-size") return tile_size->count() > 0;
            if (flag == "--no-dirty-rects") return no_dirty_rects->count() > 0;
            if (flag == "--motion-blur") return motion_blur->count() > 0;
            if (flag == "--motion-blur-mode") return motion_blur_mode->count() > 0;
            if (flag == "--motion-blur-samples") return motion_blur_samples->count() > 0;
            if (flag == "--warmup-framebuffers") return warmup_framebuffers->count() > 0;
            if (flag == "--diagnostic") return diagnostic->count() > 0;
            if (flag == "--program-cache-tune") return program_cache_tune->count() > 0;
            return false;
        };
        apply_render_profile(render_args, state->profile, explicitly_set);

        auto loaded = load_props_input(state->props_file, state->props_json);
        if (!loaded.ok) {
            spdlog::error("{}", loaded.error);
            ctx.exit_code = 1;
            return;
        }

        render_args.command_line = ctx.command_line;
        render_args.cpu_budget = ctx.cpu_budget;
        if (render_args.output.empty()) {
            std::filesystem::path comp_path(render_args.comp_id);
            std::string comp_basename = comp_path.stem().string();
            if (comp_basename.empty()) comp_basename = "render";
            render_args.output = "output/" + comp_basename + ".png";
            spdlog::info("No output path specified, defaulting to {}", render_args.output);
        }
        set_log_level(render_args.log_level);

        auto job = make_render_job(ctx.registry, render_args, loaded.props);
        if (!job) {
            ctx.exit_code = 1;
            return;
        }
        auto video_execution = std::make_shared<media::VideoJobExecutionContext>();
        video_execution->video_runtimes = ctx.video_runtimes;
        auto result = execute_render_job(*job, {}, std::move(video_execution));
        if (!result) {
            if (job->mode == RenderMode::Video) print_render_error(result.error(), *job);
            else spdlog::error("Render job failed: {}", result.error().message);
            ctx.exit_code = 1;
            return;
        }
        ctx.exit_code = 0;
    });
}
