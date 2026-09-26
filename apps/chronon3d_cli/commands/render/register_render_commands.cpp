#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../daemon/daemon_service.hpp"
#include "../../utils/common/cli_mappers.hpp"
#include "../../utils/common/props_file.hpp"
#include "../../utils/common/props_inline.hpp"
#include "../../utils/common/render_job_error_formatter.hpp"
#include "../../utils/job/render_job.hpp"
#include "render_profiles.hpp"
#include "command_render_plan.hpp"
#include "../../utils/project/chronon_project_render.hpp"
#include "render_plan_preparation.hpp"
#include "caption_plan_expansion.hpp"
#include "../video/common/pipe_pixfmt_vocabulary.hpp"
// ONE sink vocabulary for the --video-sink option checker (sink_options.hpp).
#include "../video/common/sink_options.hpp"
#include "../../utils/video/encoder_config_resolution.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <limits>
#include <cstdint>

namespace chronon3d::cli {

namespace {

struct RenderState {
    std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()};
    std::string profile{"production"};
    std::string props_file;
    std::string props_json;
    std::string plan_file;
    std::string captions_file;
    std::string caption_preset{"minimal_white"};
    std::string caption_font;
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
    marked("--video-sink", vs.sink_type_explicit);
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
    // The accepted names come from ONE vocabulary
    // (video_sink_type_names(), sink_options.hpp), so this option can never
    // advertise a value the sink parser rejects. `raw` is the pre-encode truth
    // surface: rendered host frames are written verbatim with no encoder, which
    // is what pixel-level quality diagnosis must measure.
    video->add_option("--video-sink", args.video_settings.sink_type,
                      "Output sink: ffmpeg (encode+mux) or raw (pre-encode frames, no encoder)")
        ->check(CLI::IsMember(video_sink_type_names()));
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
void register_render_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    // Phase 9 (CLI/IPC parity): ONE typed dispatcher over the normalized
    // job. The daemon parsed the wire JSON once and normalized once; this
    // registration only executes. `warm_renderer` is runtime state (null for
    // Direct-YUV), never a second execution authority.
    render_job_dispatcher() = [registry = &ctx.registry](
        const runtime::job::NormalizedRenderJob& job,
        std::shared_ptr<SoftwareRenderer> renderer,
        std::shared_ptr<media::VideoJobExecutionContext> video_execution) -> ipc::Reply {
        return ipc_render_job(*registry, job, std::move(renderer),
                              std::move(video_execution));
    };

    auto* cmd = app.add_subcommand("render", "Render a composition");
    cmd->add_option("input", args.comp_id, "Composition name");
    cmd->add_option("--plan", state->plan_file, "Render a chronon.render-plan.v2 file through the canonical executor");
    cmd->add_option("--captions", state->captions_file, "Timed text file (SRT, ASS, or JSON) expanded into text layers");
    cmd->add_option("--caption-preset", state->caption_preset, "Caption motion preset: minimal_white, karaoke, word_pop, or typewriter");
    cmd->add_option("--caption-font", state->caption_font, "Explicit logical font asset when the plan has no text style");
    cmd->add_option("--prepared-package", state->prepared_package,
                    "Immutable prepared overlay package sidecar");
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("--frame", args.frames, "Single frame number (alias for --frames)");
    // Explicit chunk range for the --plan route (mirrors the render-plan
    // subcommand). Only the presence of both bounds makes 0..0 a real
    // single-frame chunk at frame 0; an absent --end-frame keeps the legacy
    // "render through the last frame" default.
    auto* start_frame_opt = cmd->add_option("--start-frame", state->start_frame, "First frame (render --plan route)");
    auto* end_frame_opt = cmd->add_option("--end-frame", state->end_frame, "Last frame, inclusive (render --plan route)");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_option("--assets-root", args.assets_root, "Root directory for resolving relative assets");
    cmd->add_option("--backend", state->backend, "Render backend: auto, software, or vulkan (strict)")
        ->default_val(std::string{media::kDefaultBackend})
        ->check(CLI::IsMember({"auto", "software", "vulkan"}));
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
        diagnostic, program_cache_tune, start_frame_opt, end_frame_opt
    ]() {
        auto& render_args = *state->args;
        commit_typed_runtime_options(*state);
        mark_video_option_explicitness(*cmd, render_args);
        // An explicit --frames/--frame range is carried as explicit so a
        // single-frame chunk at frame 0 ("--frames 0") is never re-expanded
        // into a whole-plan render (legacy default when the flag is absent).
        render_args.frames_explicit =
            (cmd->get_option("--frames")->count() > 0) ||
            (cmd->get_option("--frame")->count() > 0);
        if (auto* pipe_pixfmt = cmd->get_option("--pipe-pixfmt")) {
            render_args.pipe_pixfmt_explicit = pipe_pixfmt->count() > 0;
        }
        if (!state->plan_file.empty()) {
            CaptionPlanExpansion expanded_captions;
            std::string active_plan = state->plan_file;
            if (!state->captions_file.empty()) {
                expanded_captions = expand_caption_plan(
                    state->plan_file, state->captions_file, state->caption_preset,
                    state->caption_font);
                if (!expanded_captions.error.empty()) {
                    spdlog::error("[captions] {}", expanded_captions.error);
                    ctx.exit_code = 1;
                    return;
                }
                active_plan = expanded_captions.plan_path.string();
            }
            if (render_args.video_settings.dry_run) {
                RenderPlanPreparationOptions options;
                options.input = active_plan;
                options.assets_root = render_args.assets_root;
                options.prepared_package = state->prepared_package;
                if (!state->captions_file.empty() && options.assets_root.empty() &&
                    options.prepared_package.empty())
                    options.assets_root = std::filesystem::absolute(state->plan_file)
                        .parent_path().string();
                ctx.exit_code = validate_render_plan(options);
                return;
            }
            runtime::job::RenderJobRequest request;
            request.plan_path = active_plan;
            request.prepared_package_path = state->prepared_package;
            request.output_path = render_args.output;
            request.assets_root = render_args.assets_root;
            if (!state->captions_file.empty() && request.assets_root.empty() &&
                request.prepared_package_path.empty())
                request.assets_root = std::filesystem::absolute(state->plan_file)
                    .parent_path().string();
            request.backend = backend_cli_name(render_args.backend);
            request.report = render_args.report;
            request.trace.output = render_args.trace_output;
            request.trace.level = render_args.trace_level;
            request.output.codec = render_args.video_settings.codec;
            // USER INTENT vs CHRONON DEFAULTS (the --end-frame count pattern).
            // A strict hot-path request is a GPU-native execution contract: an
            // explicit encoder transport/hardware selection always travels and
            // wins, while an unmentioned pair stays empty so normalization
            // back-fills the canonical derivation instead of letting the
            // VideoSettings defaults pin "none" over a strict request
            // (TICKET-CLI-STRICT-GPU-NATIVE-MAPPING).
            const auto explicit_flag = [&cmd](const char* name) {
                const auto* option = cmd->get_option(name);
                return option != nullptr && option->count() > 0;
            };
            const bool strict_gpu_hot_path =
                render_args.gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
                render_args.gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv;
            if (explicit_flag("--hardware") || !strict_gpu_hot_path)
                request.output.hardware_encoder =
                    render_args.video_settings.hardware_encoder;
            if (explicit_flag("--encoder-backend") || !strict_gpu_hot_path)
                request.output.encoder_backend =
                    render_args.video_settings.encoder_backend;
            request.output.ffmpeg_mode = render_args.video_settings.ffmpeg_mode;
            request.output.gpu_hot_path_mode =
                std::string(chronon3d::to_string(render_args.gpu_hot_path_mode));
            request.output.encode_preset = render_args.video_settings.encode_preset_explicit
                ? render_args.video_settings.encode_preset : std::string{};
            request.output.rate_control_mode = render_args.video_settings.rate_control_mode_explicit
                ? render_args.video_settings.rate_control_mode : std::string{};
            request.output.crf = render_args.video_settings.crf_explicit
                ? render_args.video_settings.crf : -1;
            request.output.qp = render_args.video_settings.qp_explicit
                ? render_args.video_settings.qp : -1;
            request.output.bitrate = render_args.video_settings.bitrate_explicit
                ? render_args.video_settings.bitrate : std::int64_t{0};
            request.output.gop_source = render_args.video_settings.gop_source;
            request.output.audio_source_path = render_args.video_settings.gop_source;
            request.output.audio_target_sample_rate =
                render_args.video_settings.audio_target_sample_rate;
            request.output.gop_copy_only = render_args.video_settings.gop_copy_only;
            request.output.changed_intervals = render_args.video_settings.changed_intervals;
            if (render_args.pipe_pixfmt_explicit)
                request.output.pipe_pixfmt = render_args.video_settings.pipe_pixfmt;
            // Only an explicit request travels; an unmentioned sink stays empty
            // so normalization back-fills the canonical default and a plan-file
            // job fingerprints identically to a daemon/IPC job.
            if (render_args.video_settings.sink_type_explicit)
                request.output.sink_type = render_args.video_settings.sink_type;
            if (end_frame_opt->count() > 0) {
                request.frame_range = runtime::job::FrameRange{
                    static_cast<std::int64_t>(state->start_frame),
                    static_cast<std::int64_t>(state->end_frame)};
            }
            // The semantic requirements derive from the hot-path selection as
            // well as the hardware spelling, and a strict request forbids CPU
            // fallback: that is the shape resolve_canonical_execution_parameters
            // needs to derive the canonical native lane for CLI and IPC jobs
            // alike (TICKET-CLI-STRICT-GPU-NATIVE-MAPPING).
            request.requirements.gpu_required =
                hardware_nvenc_requested(
                    render_args.video_settings.hardware_encoder) ||
                strict_gpu_hot_path;
            request.requirements.cpu_fallback_allowed = !strict_gpu_hot_path;
            auto video_execution = std::make_shared<media::VideoJobExecutionContext>();
            video_execution->video_runtimes = ctx.video_runtimes;
            ctx.exit_code = run_render_plan_file(
                ctx.registry, std::move(request), {}, std::move(video_execution));
            return;
        }
        if (!state->captions_file.empty()) {
            spdlog::error("--captions requires --plan so captions can be expanded before compilation");
            ctx.exit_code = 1;
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

        std::string project_dispatch_diagnostic;
        const auto project_dispatch = dispatch_project_render(
            std::filesystem::current_path(), render_args.comp_id,
            render_args.output, project_dispatch_diagnostic);
        if (project_dispatch) {
            if (!project_dispatch_diagnostic.empty()) {
                spdlog::error("{}", project_dispatch_diagnostic);
            }
            ctx.exit_code = *project_dispatch;
            return;
        }

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

} // namespace chronon3d::cli
