#include "render_job.hpp"
#include "../common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>

namespace chronon3d::cli {

namespace {

bool is_video_output(const std::string& output) {
    std::string ext = std::filesystem::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp4" || ext == ".mov" || ext == ".mkv" ||
           ext == ".webm";
}

void finalize_video_settings(RenderJob& job) {
    if (job.video_settings.frames_dir.empty()) {
        job.video_settings.frames_dir =
            "chronon_" + std::filesystem::path(job.comp_id).filename().string();
    }

    if (job.video_settings.tune.empty() &&
        job.video_settings.codec == "libx264") {
        job.video_settings.tune = "zerolatency";
        spdlog::info(
            "[video] Auto-selecting x264 tune=zerolatency for low-latency pipe export");
    }

#if defined(__linux__)
    if (job.video_settings.pipe_pixfmt == "rgba" &&
        job.comp->width() % 2 == 0 && job.comp->height() % 2 == 0 &&
        job.video_settings.codec != "libx264rgb") {
        job.video_settings.pipe_pixfmt = "yuv420p";
        spdlog::info(
            "[video] Auto-selecting yuv420p pipe pixel format for {}x{} output",
            job.comp->width(), job.comp->height());
    }

    if (job.video_settings.pipe_writer == "io_uring") {
        spdlog::warn(
            "[video] io_uring pipe writer is experimental; use classic for stable exports");
    }
#endif
}

} // namespace

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args) {
    return make_render_job(registry, args, CompositionProps{});
}

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props) {
    const auto parsed_range = parse_frames_safe(args.frames);
    if (!parsed_range.ok) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::InvalidInput,
                "render",
                "InvalidRange: invalid frame range '" + args.frames + "': " +
                    parsed_range.error,
            },
            args.comp_id);
        return std::nullopt;
    }
    const auto& range = parsed_range.value;

    auto resolved = resolve_composition(registry, args.comp_id, props);
    if (!resolved) {
        return std::nullopt;
    }

    RenderJob job;
    job.registry = &registry;
    job.comp = std::move(resolved.comp);
    job.comp_id = args.comp_id;
    job.output = args.output;
    job.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    job.settings.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;
    job.frame_step = Frame{range.step};

    if (is_video_output(args.output)) {
        job.mode = RenderMode::Video;
        job.first_frame = Frame{range.start};

        const auto duration_last = std::max<std::int64_t>(
            range.start, job.comp->duration().integral() - 1);
        job.last_frame = (range.start == 0 && range.end == 0)
            ? Frame{duration_last}
            : Frame{range.end};

        job.video_settings = args.video_settings;
        finalize_video_settings(job);
    } else if (range.start == range.end) {
        job.mode = RenderMode::Still;
        job.still_frame = Frame{range.start};
    } else {
        job.mode = RenderMode::Sequence;
        job.first_frame = Frame{range.start};
        job.last_frame = Frame{range.end};
    }

    job.execution.log_level = args.log_level;
    job.execution.benchmark_all = args.benchmark_all;
    job.execution.report = args.report;
    job.execution.command_line = args.command_line;
    job.execution.diagnostic_plan = args.pipeline.diagnostic_plan;
    job.execution.warmup_renderer =
        args.pipeline.warmup_renderer ||
        (job.mode == RenderMode::Video && job.video_settings.ffmpeg_mode == "pipe");
    job.execution.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    job.execution.warmup_dummy_frame =
        args.pipeline.warmup_dummy_frame ||
        (job.mode == RenderMode::Video && job.video_settings.ffmpeg_mode == "pipe");
    job.execution.cpu_budget = args.cpu_budget;

    Config cfg;
    cfg.set_cpu_budget(args.cpu_budget);
    if (args.pipeline.fb_pool_budget_mb > 0) {
        cfg.set_fb_pool_budget(args.pipeline.fb_pool_budget_mb * 1024ULL * 1024ULL);
    }
    if (!args.pipeline.fb_pool_clear_policy.empty()) {
        const auto& policy_str = args.pipeline.fb_pool_clear_policy;
        if (auto parsed = chronon3d::cache::parse_framebuffer_pool_clear_policy(policy_str)) {
            cfg.set_fb_pool_clear_policy(*parsed);
        } else {
            spdlog::warn(
                "Invalid --fb-pool-clear-policy='{}'; valid values: "
                "keep-warm, trim-after-job, trim-on-memory-pressure. "
                "Using env-resolved default.",
                policy_str);
        }
    }
    job.execution.config = std::move(cfg);

    return job;
}

} // namespace chronon3d::cli
