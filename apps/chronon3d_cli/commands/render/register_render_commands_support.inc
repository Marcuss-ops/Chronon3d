namespace {

struct RenderState {
    std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()};
    std::string profile{"production"};
    std::string props_file;
    std::string props_json;
    std::string plan_file;
};

std::string lower_profile(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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
    if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error" || level == "err") {
        spdlog::set_level(spdlog::level::err);
    }
}

void add_video_options(CLI::App& cmd, RenderArgs& args) {
    auto* video = cmd.add_option_group(
        "Video output",
        "Encoder controls used when the output extension selects video mode");

    video->add_option("--fps", args.video_settings.fps, "Output frame rate")
        ->check(CLI::Range(1, 240));
    video->add_option("--rate-control", args.video_settings.rate_control_mode,
                      "Rate control: crf, qp, or bitrate")
        ->check(CLI::IsMember({"crf", "qp", "bitrate"}));
    video->add_option("--crf", args.video_settings.crf,
                      "Encoder CRF quality (0-51)")
        ->check(CLI::Range(0, 51));
    video->add_option("--qp", args.video_settings.qp,
                      "Encoder constant QP quality (0-63)")
        ->check(CLI::Range(0, 63));
    video->add_option("--bitrate", args.video_settings.bitrate,
                      "Target bitrate in bits/second")
        ->check(CLI::Range(static_cast<std::int64_t>(1), std::numeric_limits<std::int64_t>::max()));
    video->add_option("--codec", args.video_settings.codec,
                      "Video codec/encoder name");
    video->add_option("--encode-preset,--preset", args.video_settings.encode_preset,
                      "Encoder preset");
    video->add_option("--tune", args.video_settings.tune, "Encoder tune");
    video->add_option("--hardware", args.video_settings.hardware_encoder,
                      "Hardware encoder: none, auto, nvenc, qsv, videotoolbox, amf");
    video->add_flag("--keep-frames", args.video_settings.keep_frames,
                    "Keep temporary PNG frames");
    video->add_option("--frames-dir", args.video_settings.frames_dir,
                      "Override temporary frames directory");
    video->add_option("--chunks", args.video_settings.chunks,
                      "Render frame range in N chunks")
        ->check(CLI::Range(1, 1024));
    video->add_option("--gop-source", args.video_settings.gop_source,
                      "Compressed video source for GOP smart-copy analysis");
    video->add_flag("--gop-copy-only", args.video_settings.gop_copy_only,
                    "Copy compressed video/audio packets without rendering (explicit source-equivalence contract)");
    video->add_option("--ffmpeg-mode", args.video_settings.ffmpeg_mode,
                      "FFmpeg mode: pipe or png")
        ->check(CLI::IsMember({"pipe", "png"}));
    video->add_flag("--ffmpeg-verbose", args.video_settings.ffmpeg_verbose,
                    "Show FFmpeg logs");
    video->add_option("--pipe-pixfmt", args.video_settings.pipe_pixfmt,
                      "Pipe pixel format")
        ->check(CLI::IsMember({"rgba", "yuv420p", "nv12"}));
    video->add_option("--color-output", args.video_settings.color_output,
                      "Output color space")
        ->check(CLI::IsMember({"srgb", "rec709", "linearsrgb"}));
    video->add_option("--pipe-writer", args.video_settings.pipe_writer,
                      "Pipe writer: classic or io_uring")
        ->check(CLI::IsMember({"classic", "io_uring"}));
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    video->add_option("--encoder-backend", args.video_settings.encoder_backend,
                      "Encoder backend: pipe or native")
        ->check(CLI::IsMember({"pipe", "native"}));
#else
    video->add_option("--encoder-backend", args.video_settings.encoder_backend,
                      "Encoder backend: external FFmpeg pipe")
        ->check(CLI::IsMember({"pipe"}));
#endif
    video->add_flag("--dry-run", args.video_settings.dry_run,
                    "Validate without rendering (video settings; render-plan validation with --plan)");
}

} // namespace
