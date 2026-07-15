#include "render_job.hpp"
#include "../common/render_error_formatter.hpp"

#include "../common/cli_utils.hpp"

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

namespace {

void fill_execution_options(RenderExecutionOptions& execution,
                            const RenderPipelineArgs& pipeline,
                            const CpuBudget& cpu_budget) {
    execution.warmup_renderer =
        pipeline.warmup_renderer || execution.warmup_renderer;
    execution.warmup_framebuffers = pipeline.warmup_framebuffers;
    execution.warmup_dummy_frame =
        pipeline.warmup_dummy_frame || execution.warmup_dummy_frame;
    execution.cpu_budget = cpu_budget;

    Config cfg;
    cfg.set_cpu_budget(cpu_budget);
    if (pipeline.fb_pool_budget_mb > 0) {
        cfg.set_fb_pool_budget(pipeline.fb_pool_budget_mb * 1024ULL * 1024ULL);
    }
    if (!pipeline.fb_pool_clear_policy.empty()) {
        const auto& policy_str = pipeline.fb_pool_clear_policy;
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
    execution.config = std::move(cfg);
}

} // namespace

std::optional<RenderRequest> make_render_request(const CompositionRegistry& registry,
                                                 const RenderArgs& args,
                                                 const CompositionProps& props) {
    RenderRequest request;
    request.comp_id = args.comp_id;
    request.input.values = props.values;
    request.input.project_root = props.project_root;
    request.input.assets = props.assets;
    request.output = args.output;
    request.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    request.settings.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;

    const auto range = parse_frames(args.frames);
    request.frame_step = Frame{range.step};

    if (is_video_output(args.output)) {
        request.mode = RenderMode::Video;
        request.first_frame = Frame{range.start};

        // `render Comp -o out.mp4` keeps RenderArgs' historical default
        // frames="0" but means the full composition for Video mode.  An
        // explicit non-zero single frame remains a valid one-frame video.
        auto resolved = resolve_composition(registry, args.comp_id, props);
        if (!resolved) return std::nullopt;
        const auto duration_last = std::max<std::int64_t>(
            range.start, resolved.comp->duration().integral() - 1);
        request.last_frame = (range.start == 0 && range.end == 0)
            ? Frame{duration_last}
            : Frame{range.end};

        request.video_settings.frames_dir =
            "chronon_" + std::filesystem::path(args.comp_id).filename().string();
    } else if (range.start == range.end) {
        request.mode = RenderMode::Still;
        request.still_frame = Frame{range.start};
    } else {
        request.mode = RenderMode::Sequence;
        request.first_frame = Frame{range.start};
        request.last_frame = Frame{range.end};
    }

    request.execution.log_level = args.log_level;
    request.execution.benchmark_all = args.benchmark_all;
    request.execution.report = args.report;
    request.execution.command_line = args.command_line;
    request.execution.diagnostic_plan = args.pipeline.diagnostic_plan;
    if (request.mode == RenderMode::Video) {
        request.execution.warmup_renderer = true;
    }
    fill_execution_options(request.execution, args.pipeline, args.cpu_budget);

    return request;
}

std::optional<RenderRequest> make_render_request(const CompositionRegistry& /*registry*/,
                                                 const StillArgs& args) {
    RenderRequest request;
    request.comp_id = args.comp_id;
    request.mode = RenderMode::Still;
    request.still_frame = args.frame;
    request.output = args.output;
    request.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    request.execution.log_level = args.log_level;
    request.execution.diagnostic_plan = args.pipeline.diagnostic_plan;
    fill_execution_options(request.execution, args.pipeline, {});
    return request;
}

std::optional<RenderRequest> make_render_request(const CompositionRegistry& registry,
                                                 const VideoArgs& args) {
    if (args.comp_id.empty()) {
        spdlog::error("[video] No composition specified.");
        return std::nullopt;
    }

    const std::string output = args.output.empty()
        ? chronon_artifact_path(
              "videos",
              std::filesystem::path(args.comp_id).filename().string() + ".mp4")
              .string()
        : args.output;

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return std::nullopt;

    const auto& comp = *resolved.comp;
    const Frame end_exclusive = (args.end > args.start)
        ? args.end
        : comp.duration();

    RenderRequest request;
    request.comp_id = args.comp_id;
    request.mode = RenderMode::Video;
    request.first_frame = args.start;
    request.last_frame = Frame{end_exclusive.integral() - 1};
    request.output = output;
    request.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    request.settings.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;

    request.video_settings.fps = args.fps;
    request.video_settings.crf = args.crf;
    request.video_settings.codec = args.codec;
    request.video_settings.encode_preset = args.encode_preset;
    request.video_settings.tune = args.tune;
    request.video_settings.keep_frames = args.keep_frames;
    request.video_settings.frames_dir = args.frames_dir.empty()
        ? ("chronon_" + std::filesystem::path(args.comp_id).filename().string())
        : args.frames_dir;
    request.video_settings.chunks = args.chunks;
    request.video_settings.hardware_encoder = args.hardware_encoder;
    request.video_settings.ffmpeg_mode = args.ffmpeg_mode;
    request.video_settings.ffmpeg_verbose = args.ffmpeg_verbose;
    request.video_settings.pipe_pixfmt = args.pipe_pixfmt;
    request.video_settings.color_output = args.color_output;
    request.video_settings.pipe_writer = args.pipe_writer;
    request.video_settings.encoder_backend = args.encoder_backend;
    request.video_settings.sink_type = "ffmpeg";
    request.video_settings.dry_run = args.dry_run;

    request.execution.log_level = args.log_level;
    request.execution.command_line = args.command_line;
    request.execution.diagnostic_plan = args.pipeline.diagnostic_plan;
    request.execution.warmup_renderer =
        args.pipeline.warmup_renderer || args.ffmpeg_mode == "pipe";
    request.execution.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    request.execution.warmup_dummy_frame =
        args.pipeline.warmup_dummy_frame || args.ffmpeg_mode == "pipe";
    request.execution.cpu_budget = args.cpu_budget;
    fill_execution_options(request.execution, args.pipeline, args.cpu_budget);

    return request;
}

Result<ResolvedRenderJob, RenderJobError> resolve_render_request(
    const CompositionRegistry& registry,
    RenderRequest request) {
    auto resolved = registry.resolve(request.comp_id, request.input);
    if (!resolved) {
        const auto& err = resolved.error();
        return RenderJobError{
            RenderJobErrorCode::ValidationFailed,
            "Failed to resolve composition '" + request.comp_id + "': " + err.message};
    }

    auto desc = registry.descriptor_of(request.comp_id);
    if (!desc || !desc->factory) {
        return RenderJobError{
            RenderJobErrorCode::InvalidJob,
            "Composition '" + request.comp_id + "' has no factory"};
    }

    Composition comp = desc->factory(resolved->props);
    auto comp_ptr = std::make_shared<const Composition>(std::move(comp));

    ResolvedRenderJob job;
    job.request = std::move(request);
    job.comp = std::move(comp_ptr);
    job.metadata = resolved->metadata.value_or(CompositionMetadata{});
    job.registry = &registry;
    return job;
}

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args) {
    return make_render_job(registry, args, CompositionProps{});
}

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props) {
    auto request = make_render_request(registry, args, props);
    if (!request) {
        return std::nullopt;
    }
    auto resolved = resolve_render_request(registry, std::move(*request));
    if (!resolved) {
        spdlog::error("Failed to resolve render job: {}", resolved.error().message);
        return std::nullopt;
    }
    return resolved->to_legacy_job();
}

std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args) {
    return make_render_job(registry, args);
}

std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props) {
    return make_render_job(registry, args, props);
}
} // namespace chronon3d::cli
