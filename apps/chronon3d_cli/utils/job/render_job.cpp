#include "render_job.hpp"

#include "../common/cli_utils.hpp"

#include <chronon3d/timeline/compile_evaluate.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>

namespace chronon3d::cli {

namespace {

bool is_video_output(const std::string& output) {
    std::string ext = std::filesystem::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".webm";
}

void fill_execution_options(RenderExecutionOptions& execution,
                            const RenderPipelineArgs& pipeline,
                            const CpuBudget& cpu_budget,
                            bool video_output,
                            chronon3d::graph::BackendPreference backend_preference) {
    execution.warmup_renderer = pipeline.warmup_renderer || execution.warmup_renderer;
    execution.warmup_framebuffers = pipeline.warmup_framebuffers;
    execution.warmup_dummy_frame = pipeline.warmup_dummy_frame || execution.warmup_dummy_frame;
    execution.cpu_budget = cpu_budget;

    Config cfg;
    cfg.set_cpu_budget(cpu_budget);
    cfg.set_backend_preference(backend_preference);
    const size_t fb_pool_budget_mb = pipeline.fb_pool_budget_mb > 0
        ? pipeline.fb_pool_budget_mb
        : (video_output ? 64U : 0U);
    if (fb_pool_budget_mb > 0) {
        cfg.set_fb_pool_budget(fb_pool_budget_mb * 1024ULL * 1024ULL);
    }
    if (pipeline.fb_pool_clear_policy) {
        cfg.set_fb_pool_clear_policy(*pipeline.fb_pool_clear_policy);
    }
    execution.config = std::move(cfg);
}

void finalize_video_settings(RenderJob& job) {
    if (job.video_settings.frames_dir.empty()) {
        job.video_settings.frames_dir = "chronon_" +
            std::filesystem::path(job.comp_id).filename().string();
    }

    if (job.video_settings.tune.empty() && job.video_settings.codec == "libx264") {
        job.video_settings.tune = "zerolatency";
        spdlog::info("[video] Auto-selecting x264 tune=zerolatency for low-latency pipe export");
    }

#if defined(__linux__)
    if (job.video_settings.pipe_pixfmt.empty() &&
        job.metadata.width % 2 == 0 && job.metadata.height % 2 == 0 &&
        job.video_settings.codec != "libx264rgb") {
        job.video_settings.pipe_pixfmt = "yuv420p";
        spdlog::info("[video] Auto-selecting yuv420p pipe pixel format for {}x{} output",
                     job.metadata.width, job.metadata.height);
    }

    if (job.video_settings.pipe_writer == "io_uring") {
        spdlog::warn("[video] io_uring pipe writer is experimental; use classic for stable exports");
    }
#endif
}

} // namespace

std::optional<RenderRequest> make_render_request(
    const CompositionRegistry& registry,
    const RenderArgs& args,
    const CompositionProps& props) {
    if (args.comp_id.empty() || !registry.contains(args.comp_id)) return std::nullopt;

    RenderRequest request;
    request.comp_id = args.comp_id;
    request.input.values = props.values;
    request.input.project_root = props.project_root;
    request.output = args.output;
    if (!args.assets_root.empty()) request.execution.assets_root = std::filesystem::path(args.assets_root);
    request.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    request.settings.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;
    request.video_settings = args.video_settings;
    if (!args.pipe_pixfmt_explicit) request.video_settings.pipe_pixfmt.clear();

    const auto range = parse_frames(args.frames);
    request.frame_step = Frame{range.step};

    if (is_video_output(args.output)) {
        request.mode = RenderMode::Video;
        request.first_frame = Frame{range.start};
        request.last_frame = Frame{range.end};
        request.execution.warmup_renderer = true;
        request.execution.warmup_dummy_frame = true;
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
    request.execution.trace_output = std::filesystem::path(args.trace_output);
    request.execution.trace_level = args.trace_level;
    fill_execution_options(request.execution, args.pipeline, args.cpu_budget,
                           request.mode == RenderMode::Video, args.backend);

    return request;
}

Result<RenderJob, RenderJobError> resolve_render_request(
    const CompositionRegistry& registry,
    RenderRequest request) {
    const std::string composition_id = request.comp_id;
    std::shared_ptr<const CompiledComposition> compiled = std::move(request.compiled_composition);
    std::optional<ResolvedCompositionSpec> resolved;
    if (!compiled) {
        auto registry_result = registry.resolve(composition_id, request.input);
        if (!registry_result) {
            return RenderJobError{RenderJobErrorCode::ValidationFailed,
                "Failed to resolve composition '" + composition_id + "': " + registry_result.error().message};
        }
        resolved = std::move(registry_result.value());
        if (!resolved->construct) {
            return RenderJobError{RenderJobErrorCode::InvalidJob,
                "Composition '" + composition_id + "' has no prepared constructor"};
        }
    }

    try {
        if (!compiled) {
            Composition comp = resolved->construct();
            auto compiled_result = chronon3d::compile_composition(comp, CompositionCompileContext{});
            if (!compiled_result) {
                return RenderJobError{RenderJobErrorCode::SetupFailed,
                    "Composition compilation failed for '" + composition_id + "': " + compiled_result.error().message};
            }
            compiled = std::make_shared<const CompiledComposition>(std::move(compiled_result).value());
        }

        RenderJob job;
        job.registry = &registry;
        job.comp_id = std::move(request.comp_id);
        job.compiled = std::move(compiled);
        job.metadata = CompositionMetadata{
            .width = job.compiled->composition->width(),
            .height = job.compiled->composition->height(),
            .fps = job.compiled->composition->frame_rate(),
            .duration = job.compiled->composition->duration(),
        };
        if (resolved && resolved->metadata) job.metadata = *resolved->metadata;
        job.mode = request.mode;
        job.still_frame = request.still_frame;
        job.first_frame = request.first_frame;
        job.last_frame = request.last_frame;
        job.frame_step = request.frame_step;
        job.output = std::move(request.output);
        job.settings = std::move(request.settings);
        job.video_settings = std::move(request.video_settings);
        job.execution = std::move(request.execution);
        job.diagnostics = request.diagnostics;

        if (job.mode == RenderMode::Video && job.first_frame == Frame{0} && job.last_frame == Frame{0}) {
            const auto duration_last = std::max<std::int64_t>(0, job.metadata.duration.integral() - 1);
            job.last_frame = Frame{duration_last};
        }
        if (job.mode == RenderMode::Video) finalize_video_settings(job);
        return job;
    } catch (const std::exception& error) {
        return RenderJobError{RenderJobErrorCode::SetupFailed,
            "Composition construction failed for '" + composition_id + "': " + error.what()};
    }
}

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry, const RenderArgs& args) {
    return make_render_job(registry, args, CompositionProps{});
}

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props) {
    auto request = make_render_request(registry, args, props);
    if (!request) return std::nullopt;
    auto resolved = resolve_render_request(registry, std::move(*request));
    if (!resolved) {
        spdlog::error("Failed to resolve render job: {}", resolved.error().message);
        return std::nullopt;
    }
    return std::move(resolved).value();
}

} // namespace chronon3d::cli
