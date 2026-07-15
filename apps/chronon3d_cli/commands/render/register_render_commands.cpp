#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../utils/common/props_file.hpp"
#include "../../utils/job/render_job.hpp"
#include "render_profiles.hpp"

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

namespace chronon3d::cli {

namespace {

struct RenderState {
    std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()};
    std::string profile{"production"};
    std::string props_file;
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
        "  --graph / --no-graph\n"
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
    video->add_option("--crf", args.video_settings.crf,
                      "Encoder quality (0-51, lower is higher quality)")
        ->check(CLI::Range(0, 51));
    video->add_option("--codec", args.video_settings.codec,
                      "Video codec/encoder name");
    video->add_option("--encode-preset,--preset", args.video_settings.encode_preset,
                      "Encoder preset");
    video->add_option("--tune", args.video_settings.tune,
                      "Encoder tune");
    video->add_option("--hardware", args.video_settings.hardware_encoder,
                      "Hardware encoder: none, auto, nvenc, qsv, videotoolbox, amf");
    video->add_flag("--keep-frames", args.video_settings.keep_frames,
                    "Keep temporary PNG frames");
    video->add_option("--frames-dir", args.video_settings.frames_dir,
                      "Override temporary frames directory");
    video->add_option("--chunks", args.video_settings.chunks,
                      "Render frame range in N chunks")
        ->check(CLI::Range(1, 1024));
    video->add_option("--ffmpeg-mode", args.video_settings.ffmpeg_mode,
                      "FFmpeg mode: pipe or png")
        ->check(CLI::IsMember({"pipe", "png"}));
    video->add_flag("--ffmpeg-verbose", args.video_settings.ffmpeg_verbose,
                    "Show FFmpeg logs");
    video->add_option("--pipe-pixfmt", args.video_settings.pipe_pixfmt,
                      "Pipe pixel format")
        ->check(CLI::IsMember({"rgba", "yuv420p", "nv12", "yuv444p"}));
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
                    "Validate video settings without rendering");
}

} // namespace

void register_render_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand("render", "Render a composition");
    cmd->add_option("input", args.comp_id, "Composition name")->required();
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("--frame", args.frames, "Single frame number (alias for --frames)");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_option("--props-file", state->props_file,
                    "Flat JSON object containing composition props");
    cmd->add_option("--profile", state->profile,
                    "Render profile: draft | preview | production | maximum")
        ->default_val("production")
        ->transform([](std::string value) { return lower_profile(std::move(value)); })
        ->check(CLI::IsMember(
            std::set<std::string>{"draft", "preview", "production", "maximum"},
            CLI::ignore_case));
    cmd->add_option("-v,--log-level", args.log_level,
                    "Log level: trace | debug | info | warn | error");

    add_video_options(*cmd, args);

    auto* advanced = cmd->add_option_group(
        "Advanced",
        "Low-level renderer, memory, cache and diagnostic controls");
    auto* diagnostic = advanced->add_flag(
        "--diagnostic,--layout-preview", args.pipeline.diagnostic,
        "Enable layout preview overlays");
    advanced->add_flag("--diagnostic-plan", args.pipeline.diagnostic_plan,
                       "Log graph preflight diagnostics before each frame");
    advanced->add_option("--diagnostic-plan-output", args.pipeline.diagnostic_plan_output,
                         "Write graph preflight report to a path pattern");
    advanced->add_flag("--graph,!--no-graph", args.pipeline.use_modular_graph,
                       "Use modular RenderGraph path");
    auto* no_dirty_rects = advanced->add_flag(
        "--no-dirty-rects", args.pipeline.no_dirty_rects,
        "Disable dirty-rectangle invalidation");
    auto* tile_size = advanced->add_option(
        "--tile-size", args.pipeline.tile_size,
        "Tile size for dirty-rect execution");
    auto* motion_blur = advanced->add_flag(
        "--motion-blur", args.pipeline.quality.motion_blur,
        "Enable temporal motion blur");
    auto* motion_blur_mode = advanced->add_option(
        "--motion-blur-mode", args.pipeline.quality.motion_blur_mode,
        "Motion blur mode: 0=off, 1=temporal, 2=velocity");
    auto* motion_blur_samples = advanced->add_option(
        "--motion-blur-samples", args.pipeline.quality.motion_blur_samples,
        "Subframe samples");
    advanced->add_option("--shutter-angle", args.pipeline.quality.shutter_angle_deg,
                         "Shutter angle in degrees");
    advanced->add_option("--shutter-phase", args.pipeline.quality.shutter_phase_deg,
                         "Shutter phase in degrees");
    advanced->add_option("--motion-blur-pattern", args.pipeline.quality.motion_blur_pattern,
                         "Sample pattern: 0=Uniform, 1=Stratified, 2=Halton");
    advanced->add_option("--motion-blur-filter", args.pipeline.quality.motion_blur_filter,
                         "Reconstruction filter: 0=Box, 1=Triangle, 2=Gaussian");
    advanced->add_option("--ssaa", args.pipeline.quality.ssaa,
                         "Super-sampling factor");
    advanced->add_flag("--benchmark-all", args.benchmark_all,
                       "Write detailed graph-node phase durations");
    advanced->add_flag("--report", args.report,
                       "Generate an execution report log");
    advanced->add_flag("--debug-text-layout", args.pipeline.text_layout_debug,
                       "Draw text layout debug overlay and emit structured logs");
    advanced->add_flag("--diagnostic-overlay", args.pipeline.diagnostic_overlay,
                       "Draw bbox, anchor and baseline overlays");
    advanced->add_flag("--diagnostic-overlay-only", args.pipeline.diagnostic_overlay_only,
                       "Draw diagnostic overlay on a transparent background");
    advanced->add_option("--debug-text-layout-json", args.pipeline.text_layout_debug_json_path,
                         "Write per-TextRun bounds JSON");
    advanced->add_flag("--force-scalar-normal-blend", args.pipeline.force_scalar_normal_blend,
                       "Force scalar Normal blend for regression diagnosis");
    advanced->add_flag("--warmup-renderer", args.pipeline.warmup_renderer,
                       "Preallocate framebuffers and prime caches");
    auto* warmup_framebuffers = advanced->add_option(
        "--warmup-framebuffers", args.pipeline.warmup_framebuffers,
        "Number of framebuffers to preallocate");
    advanced->add_flag("--warmup-dummy-frame", args.pipeline.warmup_dummy_frame,
                       "Render a dummy frame to prime caches");
    advanced->add_option("--fb-pool-budget-mb", args.pipeline.fb_pool_budget_mb,
                         "Framebuffer pool retention budget in MB");
    advanced->add_option("--fb-pool-clear-policy", args.pipeline.fb_pool_clear_policy,
                         "keep-warm | trim-after-job | trim-on-memory-pressure");
    advanced->add_option("--program-cache-capacity", args.pipeline.program_cache_capacity,
                         "SceneProgramCache entries per Precomp node");
    auto* program_cache_tune = advanced->add_flag(
        "--program-cache-tune", args.pipeline.program_cache_tune,
        "Auto-tune SceneProgramCache capacity");
    advanced->add_option("--program-cache-tune-interval",
                         args.pipeline.program_cache_tune_interval,
                         "Frames between auto-tune checks");
    advanced->add_option("--program-cache-tune-min",
                         args.pipeline.program_cache_tune_min_capacity,
                         "Minimum capacity when down-tuning");
    advanced->add_option("--program-cache-tune-max",
                         args.pipeline.program_cache_tune_max_capacity,
                         "Maximum capacity when up-tuning");

    cmd->add_flag_callback(
        "--help-advanced",
        [] {
            print_advanced_render_help(std::cout);
            throw CLI::Success();
        },
        "Print advanced render options and exit");

    cmd->allow_windows_style_options();
    cmd->callback([
        state, &ctx, tile_size, no_dirty_rects, motion_blur,
        motion_blur_mode, motion_blur_samples, warmup_framebuffers,
        diagnostic, program_cache_tune
    ]() {
        auto& render_args = *state->args;
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

        auto loaded = load_props_file(state->props_file);
        if (!loaded.ok) {
            spdlog::error("{}", loaded.error);
            ctx.exit_code = 1;
            return;
        }
        loaded.props.assets = &ctx.assets;

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
        auto result = execute_render_job(*job);
        if (!result) {
            spdlog::error("Render job failed: {}", result.error().message);
            ctx.exit_code = 1;
            return;
        }
        ctx.exit_code = 0;
    });
}

} // namespace chronon3d::cli
