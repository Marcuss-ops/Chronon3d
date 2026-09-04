namespace {

struct RenderState {
    std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()};
    std::string profile{"production"};
    std::string props_file;
    std::string props_json;
    std::string plan_file;

    // Raw CLI spellings live only at the CLI11 boundary. The callback parses
    // these once into RenderArgs' canonical runtime types.
    std::string backend{"auto"};
    std::string gpu_hot_path_mode{"auto"};
    std::string fb_pool_clear_policy;
    int motion_blur_mode{0};
    int motion_blur_pattern{1};
    int motion_blur_filter{0};
};

std::string lower_profile(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

chronon3d::graph::BackendPreference parse_backend_preference(std::string_view value) noexcept {
    if (value == "software") return chronon3d::graph::BackendPreference::Software;
    if (value == "vulkan") return chronon3d::graph::BackendPreference::GPU;
    return chronon3d::graph::BackendPreference::Auto;
}

chronon3d::MotionBlurMode parse_motion_blur_mode(int value) noexcept {
    switch (value) {
        case 1: return chronon3d::MotionBlurMode::TemporalAccumulation;
        case 2: return chronon3d::MotionBlurMode::VelocityApproximation;
        default: return chronon3d::MotionBlurMode::Off;
    }
}

chronon3d::TemporalSamplePattern parse_motion_blur_pattern(int value) noexcept {
    switch (value) {
        case 0: return chronon3d::TemporalSamplePattern::Uniform;
        case 2: return chronon3d::TemporalSamplePattern::Halton;
        default: return chronon3d::TemporalSamplePattern::Stratified;
    }
}

chronon3d::TemporalFilter parse_motion_blur_filter(int value) noexcept {
    switch (value) {
        case 1: return chronon3d::TemporalFilter::Triangle;
        case 2: return chronon3d::TemporalFilter::Gaussian;
        default: return chronon3d::TemporalFilter::Box;
    }
}

void commit_typed_runtime_options(RenderState& state) {
    auto& args = *state.args;
    args.backend = parse_backend_preference(state.backend);
    args.gpu_hot_path_mode = chronon3d::parse_gpu_hot_path_mode(state.gpu_hot_path_mode);
    args.pipeline.quality.motion_blur_mode = parse_motion_blur_mode(state.motion_blur_mode);
    args.pipeline.quality.motion_blur_pattern = parse_motion_blur_pattern(state.motion_blur_pattern);
    args.pipeline.quality.motion_blur_filter = parse_motion_blur_filter(state.motion_blur_filter);
    if (state.fb_pool_clear_policy.empty()) {
        args.pipeline.fb_pool_clear_policy.reset();
    } else {
        args.pipeline.fb_pool_clear_policy =
            chronon3d::cache::parse_framebuffer_pool_clear_policy(state.fb_pool_clear_policy);
    }
}

std::string backend_cli_name(chronon3d::graph::BackendPreference backend) {
    switch (backend) {
        case chronon3d::graph::BackendPreference::Software: return "software";
        case chronon3d::graph::BackendPreference::GPU: return "vulkan";
        case chronon3d::graph::BackendPreference::Auto: return "auto";
    }
    return "auto";
}

void print_advanced_render_help(std::ostream& out) {
    out <<
        "Chronon3D render — Advanced\n"
        "===========================\n\n"
        "Renderer pipeline:\n"
        "  --no-dirty-rects\n"
        "  --tile-size <N>\n"
        "  --motion-blur\n"
        "  --motion-blur-mode <0|1|2>\n"
        "  --motion-blur-samples <N>\n"
        "  --motion-blur-pattern <0|1|2>\n"
        "  --motion-blur-filter <0|1|2>\n"
        "  --shutter-angle <degrees>\n"
        "  --shutter-phase <degrees>\n"
        "  --ssaa <factor>\n\n"
        "Backend selection:\n"
        "  --backend <auto|software|vulkan>\n"
        "  (vulkan is strict and fails if GPU backend is unavailable)\n\n"
        "Video output (active for .mp4/.mov/.mkv/.webm):\n"
        "  --fps <N>\n"
        "  --crf <0-51>\n"
        "  --codec <name>\n"
        "  --encode-preset <name>\n"
        "  --tune <name>\n"
        "  --hardware <none|auto|nvenc|qsv|videotoolbox|amf>\n"
        "  --ffmpeg-mode <pipe|png>\n"
        "  --encoder-backend <pipe|native>\n"
        "  --keep-frames\n"
        "  --dry-run\n\n"
        "Memory and cache:\n"
        "  --warmup-renderer\n"
        "  --warmup-framebuffers <N>\n"
        "  --warmup-dummy-frame\n"
        "  --fb-pool-budget-mb <MB>\n"
        "  --fb-pool-clear-policy <policy>\n"
        "  --program-cache-capacity <N>\n"
        "  --program-cache-tune\n"
        "  --program-cache-tune-interval <N>\n"
        "  --program-cache-tune-min <N>\n"
        "  --program-cache-tune-max <N>\n\n"
        "Diagnostics:\n"
        "  --diagnostic / --layout-preview\n"
        "  --diagnostic-plan\n"
        "  --diagnostic-plan-output <path>\n"
        "  --diagnostic-overlay\n"
        "  --diagnostic-overlay-only\n"
        "  --debug-text-layout\n"
        "  --debug-text-layout-json <path>\n"
        "  --force-scalar-normal-blend\n"
        "  --benchmark-all\n"
        "  --report\n\n"
        "Normal workflow:\n"
        "  chronon render Hero -o hero.png\n"
        "  chronon render Hero --frames 0-90 -o hero.mp4\n"
        "  chronon render Hero --profile production -o hero.png\n\n"
        "Profiles: draft | preview | production | maximum\n"
        "Explicit advanced options always override profile defaults.\n";
}

void set_log_level(std::string_view level) {
    if (level == "trace") spdlog::set_level(spdlog::level::trace);
    else if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "info") spdlog::set_level(spdlog::level::info);
    else if (level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (level == "error" || level == "err") spdlog::set_level(spdlog::level::err);
}

void add_video_options(CLI::App& cmd, RenderArgs& args) {
    auto* video = cmd.add_option_group("Video output", "Encoder controls used when the output extension selects video mode");
    video->add_option("--fps", args.video_settings.fps, "Output frame rate")->check(CLI::Range(1, 240));
    video->add_option("--rate-control", args.video_settings.rate_control_mode, "Rate control: crf, qp, or bitrate")->check(CLI::IsMember({"crf", "qp", "bitrate"}));
    video->add_option("--crf", args.video_settings.crf, "Encoder CRF quality (0-51)")->check(CLI::Range(0, 51));
    video->add_option("--qp", args.video_settings.qp, "Encoder constant QP quality (0-63)")->check(CLI::Range(0, 63));
    video->add_option("--bitrate", args.video_settings.bitrate, "Target bitrate in bits/second")->check(CLI::Range(static_cast<std::int64_t>(1), std::numeric_limits<std::int64_t>::max()));
    video->add_option("--codec", args.video_settings.codec, "Video codec/encoder name");
    video->add_option("--encode-preset,--preset", args.video_settings.encode_preset, "Encoder preset");
    video->add_option("--tune", args.video_settings.tune, "Encoder tune");
    video->add_option("--hardware", args.video_settings.hardware_encoder, "Hardware encoder: none, auto, nvenc, qsv, videotoolbox, amf");
    video->add_flag("--keep-frames", args.video_settings.keep_frames, "Keep temporary PNG frames");
    video->add_option("--frames-dir", args.video_settings.frames_dir, "Override temporary frames directory");
    video->add_option("--chunks", args.video_settings.chunks, "Render frame range in N chunks")->check(CLI::Range(1, 1024));
    video->add_option("--gop-source", args.video_settings.gop_source, "Compressed video source for GOP smart-copy analysis");
    video->add_flag("--gop-copy-only", args.video_settings.gop_copy_only, "Copy compressed video/audio packets without rendering (explicit source-equivalence contract)");
    video->add_option("--ffmpeg-mode", args.video_settings.ffmpeg_mode, "FFmpeg mode: pipe or png")->check(CLI::IsMember({"pipe", "png"}));
    video->add_flag("--ffmpeg-verbose", args.video_settings.ffmpeg_verbose, "Show FFmpeg logs");
    video->add_option("--pipe-pixfmt", args.video_settings.pipe_pixfmt, "Pipe pixel format")->check(CLI::IsMember({"rgba", "yuv420p", "nv12"}));
    video->add_option("--color-output", args.video_settings.color_output, "Output color space")->check(CLI::IsMember({"srgb", "rec709", "linearsrgb"}));
    video->add_option("--pipe-writer", args.video_settings.pipe_writer, "Pipe writer: classic or io_uring")->check(CLI::IsMember({"classic", "io_uring"}));
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    video->add_option("--encoder-backend", args.video_settings.encoder_backend, "Encoder backend: pipe or native")->check(CLI::IsMember({"pipe", "native"}));
#else
    video->add_option("--encoder-backend", args.video_settings.encoder_backend, "Encoder backend: external FFmpeg pipe")->check(CLI::IsMember({"pipe"}));
#endif
    video->add_flag("--dry-run", args.video_settings.dry_run, "Validate without rendering (video settings; render-plan validation with --plan)");
}

} // namespace
