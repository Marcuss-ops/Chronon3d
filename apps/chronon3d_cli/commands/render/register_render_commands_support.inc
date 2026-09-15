namespace {

struct RenderState {
    std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()};
    std::string profile{"production"};
    std::string props_file;
    std::string props_json;
    std::string plan_file;
    std::string prepared_package;

    // Raw CLI spellings live only at the CLI11 boundary. The callback parses
    // these once into RenderArgs' canonical runtime types.
    // Resolver-owned default (audit N8): the CLI must not own a second copy.
    std::string backend{std::string{media::kDefaultBackend}};
    std::string gpu_hot_path_mode{"auto"};
    std::string fb_pool_clear_policy;
    // Explicit frame range for the --plan route (render --plan ...
    // --start-frame N --end-frame M). Kept on the CLI boundary state, not in
    // RenderArgs, because composition renders express ranges via --frames.
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
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

chronon3d::media::ChangedInterval parse_changed_interval(std::string value) {
    // CLI spelling: start_seconds:end_seconds[:content|trim]. A single
    // repeated option maps directly to the canonical typed job field.
    const auto first_separator = value.find(':');
    const auto second_separator = first_separator == std::string::npos
        ? std::string::npos : value.find(':', first_separator + 1);
    if (first_separator == std::string::npos) {
        throw CLI::ValidationError(
            "--changed-interval expects start:end[:content|trim]");
    }
    try {
        const auto start_text = value.substr(0, first_separator);
        const auto end_text = value.substr(
            first_separator + 1,
            second_separator == std::string::npos
                ? std::string::npos
                : second_separator - first_separator - 1);
        std::size_t start_consumed = 0;
        std::size_t end_consumed = 0;
        const auto start = std::stod(start_text, &start_consumed);
        const auto end = std::stod(end_text, &end_consumed);
        if (start_consumed != start_text.size() || end_consumed != end_text.size()) {
            throw std::invalid_argument("trailing interval characters");
        }
        const auto kind_name = second_separator == std::string::npos
            ? std::string{"content"}
            : value.substr(second_separator + 1);
        chronon3d::media::ChangedIntervalKind kind;
        if (kind_name == "content") {
            kind = chronon3d::media::ChangedIntervalKind::Content;
        } else if (kind_name == "trim") {
            kind = chronon3d::media::ChangedIntervalKind::Trim;
        } else {
            throw CLI::ValidationError(
                "--changed-interval kind must be content or trim");
        }
        chronon3d::media::ChangedInterval interval{start, end, kind};
        if (!interval.valid()) {
            throw CLI::ValidationError(
                "--changed-interval requires finite 0 <= start < end");
        }
        return interval;
    } catch (const CLI::ValidationError&) {
        throw;
    } catch (const std::exception&) {
        throw CLI::ValidationError(
            "--changed-interval expects numeric start:end[:content|trim]");
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
        "  --changed-interval <start:end[:content|trim]> (repeatable)\n"
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

/// Mark which encoder options were explicitly requested on the command line.
///
/// The encoder-configuration resolver distinguishes USER INTENT from CHRONON
/// DEFAULTS: an engine placeholder (e.g. the default rate_control_mode="crf"
/// that every NVENC job carries) must never be treated as an explicit CRF
/// request. Only options actually present on the command line are explicit.
void mark_video_option_explicitness(CLI::App& cmd, RenderArgs& args) {
    auto& vs = args.video_settings;
    const auto marked = [&](const char* name, bool& target) {
        if (auto* option = cmd.get_option(name)) target = option->count() > 0;
    };
    marked("--rate-control", vs.rate_control_mode_explicit);
    marked("--crf", vs.crf_explicit);
    marked("--qp", vs.qp_explicit);
    marked("--bitrate", vs.bitrate_explicit);
    marked("--encode-preset", vs.encode_preset_explicit);
    marked("--preset", vs.encode_preset_explicit);
    marked("--tune", vs.tune_explicit);
}

void add_video_options(CLI::App& cmd, RenderArgs& args) {
    auto* video = cmd.add_option_group("Video output", "Encoder controls used when the output extension selects video mode");
    // F3.1 — the frame rate has ONE representation: the rational
    // fps_num/fps_den pair (VideoSettings carries no scalar fps field). The
    // legacy integer --fps flag is normalised into that rational HERE, at the
    // CLI boundary, so no downstream component ever sees a second scalar fps
    // authority that could override the rational media clock.
    video->add_option_function<int>("--fps", [&args](int value) {
        args.video_settings.fps_num = value;
        args.video_settings.fps_den = 1;
    }, "Output frame rate (frames per second; normalised to fps_num/fps_den)")
        ->check(CLI::Range(1, 240));
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
    // Output audio sample rate. 0 (absent) keeps the historical copy path; a
    // positive rate that differs from the source makes the native mux resample
    // the master audio so the artifact satisfies the declared rate.
    video->add_option("--audio-sample-rate", args.video_settings.audio_target_sample_rate,
                      "Output audio sample rate in Hz (0 = keep the source rate)")
        ->check(CLI::Range(0, 192000));
    video->add_flag("--gop-copy-only", args.video_settings.gop_copy_only, "Copy compressed video/audio packets without rendering (explicit source-equivalence contract)");
    video->add_option_function<std::string>(
        "--changed-interval",
        [&args](const std::string& value) {
            args.video_settings.changed_intervals.push_back(
                parse_changed_interval(value));
        },
        "Changed source interval: start:end[:content|trim] (repeatable)");
    video->add_option("--ffmpeg-mode", args.video_settings.ffmpeg_mode, "FFmpeg mode: pipe or png")->check(CLI::IsMember({"pipe", "png"}));
    video->add_flag("--ffmpeg-verbose", args.video_settings.ffmpeg_verbose, "Show FFmpeg logs");
    // ONE vocabulary: the accepted names come from the canonical pipe
    // pixel-format authority (see pipe_pixfmt_vocabulary.hpp), so this option
    // can never advertise a set the parser rejects — or accept one it cannot
    // parse.
    video->add_option("--pipe-pixfmt", args.video_settings.pipe_pixfmt, "Pipe pixel format")
        ->check(CLI::IsMember(pipe_pixfmt_source_names()));
    video->add_option("--color-output", args.video_settings.color_output, "Output color space")->check(CLI::IsMember({"srgb", "rec709", "linearsrgb"}));
    video->add_option("--pipe-writer", args.video_settings.pipe_writer, "Pipe writer: classic or io_uring")->check(CLI::IsMember({"classic", "io_uring"}));
    // ONE vocabulary (AUDIT F4): the accepted set is the canonical transport
    // vocabulary filtered by build capability, so this option can never
    // advertise "native" on a build that cannot execute it, nor accept a name
    // the parser rejects — and a new transport needs no edit here.
    video->add_option("--encoder-backend", args.video_settings.encoder_backend,
                      "Encoder backend: pipe or native")
        ->check(CLI::IsMember(available_encoder_transport_names()));
    video->add_flag("--dry-run", args.video_settings.dry_run, "Validate without rendering (video settings; render-plan validation with --plan)");
}

} // namespace
