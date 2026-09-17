#include "render_job.hpp"

#include "../common/cli_utils.hpp"
#include "../video/encoder_config_resolution.hpp"
// ONE sink vocabulary: the output lane is selected from the sink intent, so
// this translation unit asks the same authority the --video-sink checker uses.
#include "../../commands/video/common/sink_options.hpp"
// ONE pixel-truth authority: the lane selected below is announced through the
// same header the `render --plan` route uses (TICKET-IMAGE-PATH-PIXEL-TRUTH).
#include "output_lane_pixel_truth.hpp"

#include <chronon3d/media/video/video_config.hpp>
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
    // AUDIT P1-3 — one vocabulary. This predicate used to carry its own literal
    // list, which differed from the sink adapter's and from the asset ingest
    // tables in both membership (`.m4v`, `.webm`, `.mov`) and case handling.
    return media::video::is_supported_video_output_extension(
        std::filesystem::path(output).extension().string());
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
    // The yuv420p default belongs to the ENCODER path: it is the cheapest
    // surface the encoder accepts. A pre-encode sink must NOT inherit it — its
    // artifact IS the diagnostic truth surface, and a chroma-subsampled,
    // color-converted YUV stream would make every pixel-level measurement
    // report the converter instead of the renderer. Those sinks keep the host
    // frame format (rgba) unless the user names one explicitly.
    const auto requested_sink = try_parse_video_sink_type(job.video_settings.sink_type);
    const bool encodes = !requested_sink || video_sink_type_encodes(*requested_sink);
    if (!encodes && job.video_settings.pipe_pixfmt.empty()) {
        job.video_settings.pipe_pixfmt = "rgba";
        spdlog::info(
            "[video] Pre-encode sink: keeping the host frame format (rgba) "
            "instead of the encoder's yuv420p default");
    }
    if (encodes && job.video_settings.pipe_pixfmt.empty() &&
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

    // A range that the user EXPLICITLY requested (--frames/--frame present)
    // is carried as such. The legacy CLI default (no --frames) stays
    // non-explicit, so resolution can keep expanding the (0,0) default to the
    // whole plan for video while an explicit single-frame chunk at frame 0
    // ("--frames 0") resolves to exactly one frame.
    request.frame_range_explicit = args.frames_explicit;

    const bool gpu_native =
        (hardware_nvenc_requested(args.video_settings.hardware_encoder) &&
         is_native_encoder_transport(args.video_settings.encoder_backend) &&
         args.backend != chronon3d::graph::BackendPreference::Software) ||
        args.gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative;

    // Output intent decides the lane: a container extension selects the video
    // lane as before, and an EXPLICIT sink request does too even when the
    // output path has no container extension (the pre-encode "raw" sink writes
    // frames to a bare .raw and must never be routed to the image writer).
    const bool video_lane_requested =
        is_video_output(args.output) ||
        (args.video_settings.sink_type_explicit &&
         video_sink_name_selects_video_lane(args.video_settings.sink_type));

    if (video_lane_requested) {
        request.mode = RenderMode::Video;
        request.first_frame = Frame{range.start};
        request.last_frame = Frame{range.end};
        request.execution.warmup_renderer = true;
        request.execution.warmup_dummy_frame = !gpu_native;
    } else if (range.start == range.end) {
        request.mode = RenderMode::Still;
        request.still_frame = Frame{range.start};
    } else {
        request.mode = RenderMode::Sequence;
        request.first_frame = Frame{range.start};
        request.last_frame = Frame{range.end};
    }
    // The image lane is declared, not silently accepted: a still/sequence
    // render cannot certify what the renderer composed.
    pixel_truth::announce_lane(request.mode);

    request.execution.log_level = args.log_level;
    request.execution.benchmark_all = args.benchmark_all;
    request.execution.report = args.report;
    request.execution.command_line = args.command_line;
    request.execution.diagnostic_plan = args.pipeline.diagnostic_plan;
    request.execution.trace_output = std::filesystem::path(args.trace_output);
    request.execution.trace_level = args.trace_level;
    fill_execution_options(request.execution, args.pipeline, args.cpu_budget,
                           request.mode == RenderMode::Video, args.backend);
    if (gpu_native) {
        request.execution.warmup_dummy_frame = false;
    }

    return request;
}

Result<RenderJob, RenderJobError> resolve_render_request(
    const CompositionRegistry& registry,
    RenderRequest request) {
    const std::string composition_id = request.comp_id;
    std::shared_ptr<const CompiledComposition> compiled = std::move(request.compiled_composition);
    std::optional<ResolvedCompositionSpec> resolved;
    if (!compiled && !request.compiled_program) {
        auto registry_result = registry.resolve(composition_id, request.input);
        if (!registry_result) {
            return make_render_job_error(RenderJobErrorCode::ValidationFailed,
                "Failed to resolve composition '" + composition_id + "': " + registry_result.error().message);
        }
        resolved = std::move(registry_result.value());
        if (!resolved->construct) {
            return make_render_job_error(RenderJobErrorCode::InvalidJob,
                "Composition '" + composition_id + "' has no prepared constructor");
        }
    }

    try {
        if (!compiled && !request.compiled_program) {
            Composition comp = resolved->construct();
            auto compiled_result = chronon3d::compile_composition(comp, CompositionCompileContext{});
            if (!compiled_result) {
                return make_render_job_error(RenderJobErrorCode::SetupFailed,
                    "Composition compilation failed for '" + composition_id + "': " + compiled_result.error().message);
            }
            compiled = std::make_shared<const CompiledComposition>(std::move(compiled_result).value());
        }

    RenderJob job;
    job.registry = &registry;
    job.comp_id = std::move(request.comp_id);
    job.compiled = std::move(compiled);
    job.program = std::move(request.compiled_program);
        if (job.program) {
            const auto& metadata = job.program->metadata();
            job.metadata = CompositionMetadata{
                .width = static_cast<int>(metadata.width),
                .height = static_cast<int>(metadata.height),
                .fps = FrameRate{metadata.fps_numerator, metadata.fps_denominator},
                .duration = Frame{metadata.duration_frames},
            };
        } else if (job.compiled && job.compiled->composition) {
            job.metadata = CompositionMetadata{
                .width = job.compiled->composition->width(),
                .height = job.compiled->composition->height(),
                .fps = job.compiled->composition->frame_rate(),
                .duration = job.compiled->composition->duration(),
            };
        } else {
            return make_render_job_error(RenderJobErrorCode::InvalidJob,
                "RenderJob has neither a compiled program nor a compiled composition");
        }
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

        // The (0,0) sentinel means "whole plan" ONLY for the legacy default
        // (no explicit range). An explicit single-frame chunk starting at
        // frame 0 (first=0, last=0, frame_range_explicit=true) must survive
        // resolution as exactly one frame — see RenderRequest docs.
        if (job.mode == RenderMode::Video && !request.frame_range_explicit &&
            job.first_frame == Frame{0} && job.last_frame == Frame{0}) {
            const auto duration_last = std::max<std::int64_t>(0, job.metadata.duration.integral() - 1);
            job.last_frame = Frame{duration_last};
        }
        if (job.mode == RenderMode::Video) finalize_video_settings(job);
        return job;
    } catch (const std::exception& error) {
        return make_render_job_error(RenderJobErrorCode::SetupFailed,
            "Composition construction failed for '" + composition_id + "': " + error.what());
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
