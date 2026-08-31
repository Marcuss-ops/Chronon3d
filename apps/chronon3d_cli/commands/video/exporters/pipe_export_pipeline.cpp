#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "utils/process_start.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_preparation.hpp>

#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/native_frame_importer_factory.hpp>
#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <cuda.h>
#endif

#include <spdlog/spdlog.h>
#include <filesystem>
#include <functional>
#include <cstdlib>
#include <memory>
#include <thread>

namespace chronon3d::cli {

namespace {

/// Encoder-side SIMO fanout. The render queue carries one framebuffer and
/// every child receives the same shared owner; preparation, decode, shaping
/// and graph execution therefore happen once per frame.
class VariantFanoutEncoder final : public IVideoEncoder {
public:
    struct Child {
        std::unique_ptr<IVideoEncoder> encoder;
        FfmpegPipeOptions options;
    };

    explicit VariantFanoutEncoder(std::vector<Child> children)
        : children_(std::move(children)) {}

    bool open(const FfmpegPipeOptions&) override {
        for (auto& child : children_) {
            if (!child.encoder->open(child.options)) return false;
        }
        return !children_.empty();
    }

    void set_counters(RenderCounters* counters) override {
        counters_ = counters;
        for (auto& child : children_) child.encoder->set_counters(counters);
    }

    bool write_frame(const Framebuffer& framebuffer) override {
        for (auto& child : children_) {
            if (!child.encoder->write_frame(framebuffer)) return false;
        }
        if (counters_) {
            counters_->simo_variant_submits.fetch_add(
                children_.size(), std::memory_order_relaxed);
        }
        return true;
    }

    bool write_frame_async(const Framebuffer& framebuffer,
                           std::shared_ptr<Framebuffer> owner) override {
        for (auto& child : children_) {
            if (!child.encoder->write_frame_async(framebuffer, owner)) return false;
        }
        if (counters_) {
            counters_->simo_variant_submits.fetch_add(
                children_.size(), std::memory_order_relaxed);
        }
        return true;
    }

    bool close() override {
        bool ok = true;
        for (auto& child : children_) ok = child.encoder->close() && ok;
        return ok;
    }

    [[nodiscard]] std::uint64_t frames_written() const override {
        if (children_.empty()) return 0;
        auto frames = children_.front().encoder->frames_written();
        for (const auto& child : children_) {
            frames = std::min(frames, child.encoder->frames_written());
        }
        return frames;
    }

    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override {
        return children_.empty() ? EncoderFrameTelemetry{}
                                 : children_.front().encoder->last_frame_telemetry();
    }

private:
    std::vector<Child> children_;
    RenderCounters* counters_{nullptr};
};

} // namespace

std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes,
    runtime::DeviceScheduler* device_scheduler,
    std::shared_ptr<media::VideoJobExecutionContext> execution)
{
    // P0-1 fix(pipe): construct queue in PipeExportSession ctor.  BoundedChannel
    // holds std::mutex + std::condition_variable internally so it is neither
    // movable nor assignable — a late `session->queue = …` would be a build rot.
    // Queue capacity matches the in-flight arena count (4) so the render thread
    // blocks instead of busy-waiting when all arenas are queued.
    constexpr size_t kArenaPoolCount = 4;
    auto session = std::make_unique<PipeExportSession>(kArenaPoolCount);
    // Execution-path dispatch is owned exclusively by video_job_execute.cpp.
    // This lower-level session builder receives only renderable work; it must
    // not call the resolver or create a second authority. The caller has
    // already handled BitstreamCopy/SmartGopCopy and selected DirectYuv or
    // FullGraph before entering this function.
    bool direct_yuv_requested =
        opts.resolved_execution_path ==
            FfmpegExportOptions::ResolvedExecutionPath::DirectYuv;

    // Placement is decided once per job by DeviceScheduler. Keep the RAII
    // reservation in the session so it spans setup, encode, drain and close.
    if (execution && execution->reservation) {
        auto& injected_execution = *execution;
        if (!injected_execution.video_runtimes) {
            spdlog::error("[video] incomplete injected execution context: "
                          "reservation and persistent registry are required");
            return session;
        }
        if (injected_execution.device_id != injected_execution.reservation->device()) {
            spdlog::error("[video] injected device id does not match reservation");
            return session;
        }
        session->device_id = injected_execution.device_id;
        session->device_reservation = std::move(injected_execution.reservation);
    } else if (execution && execution->video_runtimes) {
        // Standalone CLI: the process owns the persistent registry, but no
        // daemon scheduler has injected a reservation. Use the canonical
        // default device; the registry still owns/reuses its runtime.
        session->device_id = 0;
    } else if (device_scheduler) {
        runtime::DeviceSelectionRequirements requirements;
        requirements.resources.compute_units =
            (opts.backend_preference == graph::BackendPreference::GPU ||
             direct_yuv_requested) ? 0.5f : 0.0f;
        requirements.resources.nvenc_sessions =
            opts.encoder.hardware_encoder == "nvenc" ? 1U : 0U;
        requirements.resources.nvdec_sessions =
            (opts.encoder.hardware_encoder == "nvenc" ||
            direct_yuv_requested) ? 1U : 0U;
        requirements.cuda = opts.encoder.hardware_encoder == "nvenc" ||
                            direct_yuv_requested;
        requirements.nvenc = opts.encoder.hardware_encoder == "nvenc";
        requirements.nvdec = requirements.resources.nvdec_sessions > 0;
        auto reservation = device_scheduler->reserve(requirements);
        if (!reservation) {
            spdlog::error("[video] DeviceScheduler could not reserve a compatible device");
            return session;
        }
        session->device_id = reservation->device();
        session->device_reservation = std::move(*reservation);
    } else {
        // No scheduler was supplied; use the canonical default device.
        session->device_id = 0;
    }

    // Resolve the persistent per-device GPU runtime BEFORE creating the
    // encoder: the encoder borrows the primary CUDA context + FFmpeg
    // hwdevice from it instead of creating its own.
    //
    // FAIL_CLOSED: when no registry is supplied we refuse to create a new
    // one inline. A throwaway registry would allocate a fresh CUDA context +
    // FFmpeg hwdevice for this single job, defeating the entire purpose of
    // the process-persistent VideoRuntimeRegistry (200-300ms of codec/hwdevice
    // opening churn per clip). The daemon MUST pass its shared registry via
    // VideoJobExecutionContext::video_runtimes; the standalone CLI path
    // MUST pass it via the video_runtimes parameter.
    if (!video_runtimes && execution) {
        video_runtimes = execution->video_runtimes;
    }
    if (!video_runtimes) {
        spdlog::error("[video] FAIL_CLOSED: no persistent VideoRuntimeRegistry supplied. "
                      "The daemon or CLI must pass its shared registry; a throwaway "
                      "registry would reintroduce per-clip codec/hwdevice churn.");
        return session;
    }
    const auto cuda_ordinal = execution
        ? execution->cuda_device_ordinal : -1;
    session->device_runtime = video_runtimes->get_or_create(
        session->device_id, nullptr, cuda_ordinal);
    if (!session->device_runtime) {
        spdlog::error("[video] failed to obtain the video device runtime; aborting");
        return session;
    }

    const auto& startup_trace = chronon3d::cli::startup_trace();
    session->startup_breakdown.logger_init_ms = startup_trace.logger_init_ms;
    session->startup_breakdown.cli_bootstrap_ms = startup_trace.cli_bootstrap_ms;
    session->startup_breakdown.cli_parse_ms = startup_trace.cli_parse_ms;
    session->startup_breakdown.composition_registration_ms =
        startup_trace.composition_registration_ms;
    session->startup_breakdown.plan_read_ms = startup_trace.plan_read_ms;
    session->startup_breakdown.plan_json_parse_ms = startup_trace.plan_json_parse_ms;
    session->startup_breakdown.plan_decode_validate_ms =
        startup_trace.plan_decode_validate_ms;
    session->startup_breakdown.plan_asset_resolve_ms =
        startup_trace.plan_asset_resolve_ms;
    session->startup_breakdown.plan_compile_ms = startup_trace.plan_compile_ms;
    session->opts = opts;
    // P1-B: atomic output — FFmpeg writes to a .partial temp file.
    // On success, make_pipe_export_result() renames it to the final path.
    // On failure, the .partial file is cleaned up.
    session->original_output_path = opts.output.output;
    // Keep the media extension at the end so FFmpeg can infer the muxer
    // (`file.mp4.partial` is not recognized as an MP4 output).
    const auto final_output_path = std::filesystem::path(opts.output.output);
    auto partial_output_path = final_output_path;
    const auto extension = final_output_path.extension();
    if (extension.empty()) {
        partial_output_path += ".partial";
    } else {
        partial_output_path.replace_filename(
            final_output_path.stem().string() + ".partial" + extension.string());
    }
    session->opts.output.output = partial_output_path.string();
    session->start_frame = start;
    session->end_frame = end;
    session->canvas_width = compiled.composition->width();
    session->canvas_height = compiled.composition->height();
    session->total_frames = static_cast<int64_t>(end - start);
    // Trace correlation: stable per-job id synthesized at setup (output path
    // hash ⊕ wall-clock ns) and shared by decode/render/encode so Perfetto
    // flows for this job never collide with a previous job in the same
    // process (daemon warm-renderer mode).
    session->trace_job_id = static_cast<std::uint64_t>(
        std::hash<std::string>{}(session->original_output_path)) ^
        profiling::timestamp_ns();
    session->started_at_iso =
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
        chronon3d::telemetry::TelemetryManager::get_current_iso_time();
#else
        "";
#endif

    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    // ── Resolve codec ─────────────────────────────────────────────────────
    // Uses session->opts (not the const param opts) because output.output
    // has been modified to include the .partial suffix for atomic output.
    const bool codec_auto = session->opts.encoder.codec == "auto";
    const std::string codec = codec_auto
        ? "libx264"
        : resolve_cli_ffmpeg_codec(session->opts.encoder.codec, session->opts.encoder.hardware_encoder);

    // ── Create encoder(s) ─────────────────────────────────────────────────
    if (!session->opts.variants.empty()) {
        if (session->opts.encoder.encoder_backend == "native") {
            spdlog::error("[video] SIMO variants require the shared CPU-frame encoder path; "
                          "native surface fanout is not yet supported");
            return session;
        }
        std::vector<VariantFanoutEncoder::Child> children;
        children.reserve(session->opts.variants.size());
        for (std::size_t index = 0; index < session->opts.variants.size(); ++index) {
            const auto& variant = session->opts.variants[index];
            if (variant.width == 0 || variant.height == 0) {
                spdlog::error("[video] SIMO variant {} has invalid dimensions", index);
                return session;
            }
            FfmpegExportOptions child_opts = session->opts;
            child_opts.variants.clear();
            child_opts.output.output = variant.output.empty()
                ? session->opts.output.output + ".variant" + std::to_string(index)
                : variant.output;
            if (!variant.codec.empty()) child_opts.encoder.codec = variant.codec;
            if (!variant.pixel_format.empty()) child_opts.pipe.pipe_pixfmt = variant.pixel_format;
            const auto child_codec = child_opts.encoder.codec == "auto"
                ? "libx264"
                : resolve_cli_ffmpeg_codec(child_opts.encoder.codec,
                                           child_opts.encoder.hardware_encoder);
            auto child_encoder = create_video_encoder(child_opts, {
                .device_runtime = session->device_runtime});
            if (!child_encoder) return session;
            auto child_options = make_pipe_options(
                compiled, child_opts, child_codec, cpu_budget);
            child_options.width = static_cast<int>(variant.width);
            child_options.height = static_cast<int>(variant.height);
            child_options.output_path = child_opts.output.output;
            children.push_back({std::move(child_encoder), std::move(child_options)});
        }
        session->encoder = std::make_unique<VariantFanoutEncoder>(std::move(children));
    } else {
        const auto enc_create_t0 = profiling::now();
        session->encoder = create_video_encoder(session->opts, {
            .device_runtime = session->device_runtime});
        session->startup_breakdown.encoder_create_ms = profiling::duration_ms(enc_create_t0, profiling::now());
    }
    if (!session->encoder) {
        spdlog::error("[video] Failed to create encoder");
        return session;  // encoder is null → caller checks
    }

    // Only create output directory for sinks that actually write output
    if (session->opts.sink.sink_type == VideoSinkType::Ffmpeg ||
        session->opts.sink.sink_type == VideoSinkType::RawFile) {
        if (!ensure_output_directory_exists(session->opts.output.output)) {
            return session;
        }
        for (const auto& variant : session->opts.variants) {
            const auto variant_path = variant.output.empty()
                ? session->opts.output.output
                : variant.output;
            if (!ensure_output_directory_exists(variant_path)) return session;
        }
    }

    auto pipe_options = make_pipe_options(compiled, session->opts, codec, cpu_budget);
    pipe_options.direct_yuv_mode = direct_yuv_requested;
    pipe_options.audio_start_seconds = static_cast<double>(start.integral()) /
        (opts.output.fps_value() > 0.0 ? opts.output.fps_value() : 30.0);
    pipe_options.audio_end_seconds = static_cast<double>(end.integral()) /
        (opts.output.fps_value() > 0.0 ? opts.output.fps_value() : 30.0);
    if (!session->encoder->open(pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        return session;
    }
    session->startup_breakdown.encoder_open_hw_ctx_ms = session->encoder->open_hw_ctx_ms();
    session->startup_breakdown.cuda_compositor_warmup_ms = session->encoder->cuda_compositor_warmup_ms();
    session->startup_breakdown.encoder_open_nvenc_ms = session->encoder->open_nvenc_ms();
    session->startup_breakdown.encoder_open_mux_header_ms = session->encoder->open_mux_header_ms();

    // Direct-YUV needs image decoding and asset resolution for its static
    // overlay, but it does not need the renderer-owned runtime to obtain
    // either service.  Build these small services before the renderer branch
    // so the dependency is explicit and can be removed completely when the
    // remaining FullGraph-only session wiring is split out.
    if (direct_yuv_requested) {
        session->direct_yuv_session = std::make_unique<DirectYuvSession>();
        auto& direct = *session->direct_yuv_session;
        direct.asset_resolver = std::make_unique<assets::AssetResolver>();
        if (session->opts.assets_root) {
            direct.asset_resolver->mount(*session->opts.assets_root);
        } else if (const char* env_root = std::getenv("CHRONON3D_CLI_ASSETS_ROOT");
                   env_root && *env_root) {
            direct.asset_resolver->mount(std::filesystem::path{env_root});
        }
        session->device_runtime->image_cache().set_asset_resolver(
            direct.asset_resolver.get());
    }

    // Track FFmpeg process only for ffmpeg pipe sink
    if (session->opts.sink.sink_type == VideoSinkType::Ffmpeg) {
        track_pipe_encoder_process(session->opts, *session->encoder, session->sys_metrics);
    }

    // Asset mounting is explicit and scoped to this render session.  The
    // exporter receives the same root carried by RenderJob and never falls
    // back to the process CWD.

    // Resolve Direct-YUV before constructing any renderer.  Its eligibility
    // scan and overlay preparation use only the compiled composition, the
    // small direct image cache, and the encoder CUDA context.
    if (direct_yuv_requested) {
        const auto input_t0 = profiling::now();
        std::string direct_reason;
        session->direct_yuv_session->program = DirectYuvProgram::prepare(
            compiled, session->device_runtime->image_cache(), session->device_runtime,
            direct_reason);
        session->input_open_ms = profiling::duration_ms(input_t0, profiling::now());
        if (!session->direct_yuv_selected()) {
            if (opts.gpu_hot_path_mode == GpuHotPathMode::Auto) {
                // Auto is allowed to probe Direct-YUV, but an ineligible
                // composition must continue through the canonical FullGraph
                // renderer. RequireDirectYuv remains fail-closed below.
                spdlog::info("[direct-yuv] auto candidate rejected; falling back to FullGraph: {}",
                             direct_reason);
                // Clear the non-owning ImageCache hook before releasing the
                // probe-only session. FullGraph must not retain a pointer to
                // the Direct-YUV session's resolver after the probe fails.
                session->device_runtime->image_cache().set_asset_resolver(nullptr);
                session->direct_yuv_session.reset();
                direct_yuv_requested = false;
            } else {
                spdlog::error("[direct-yuv] REQUIRE_DIRECT_YUV failed closed: {}",
                              direct_reason);
                session->direct_yuv_session->required_but_unavailable = true;
                return session;
            }
        }
        if (session->direct_yuv_selected()) {
            spdlog::info("[direct-yuv] selected for video source '{}'",
                         session->direct_yuv_session->program->video_path());
        }
    }

    // ── Create renderer only for FullGraph ───────────────────────────────
    const auto renderer_t0 = profiling::now();
    if (session->direct_yuv_selected()) {
        spdlog::info("[direct-yuv] skipped SoftwareRenderer and RenderRuntime construction");
    } else {
        session->full_graph_session = std::make_unique<FullGraphSession>();
        session->full_graph_session = std::make_unique<FullGraphSession>();
        Config renderer_cfg = Config::from_environment(cpu_budget);
        if (direct_yuv_requested) {
            // Direct-YUV execution bypasses Vulkan entirely: GPU processing is owned by
            // native NVDEC -> CUDA Compositor -> NVENC. Use Software backend preference
            // for the orchestration renderer so Vulkan instance, device, and SPIR-V
            // compute pipelines (~4.5s overhead) are never initialized.
            renderer_cfg.set_backend_preference(graph::BackendPreference::Software);
            spdlog::info("[direct-yuv] RequireDirectYuv active: bypassed Vulkan backend initialization");
        } else {
            renderer_cfg.set_backend_preference(opts.backend_preference);
        }
        renderer_cfg.set_gpu_hot_path_mode(opts.gpu_hot_path_mode);
        session->full_graph_session->renderer = create_renderer(
            registry, settings, std::move(renderer_cfg), session->opts.assets_root,
            &session->engine_init_ms, &session->backend_init_ms);
    }
    const auto renderer_t1 = profiling::now();

    if (session->full_graph_session && session->full_graph_session->renderer &&
        session->full_graph_session->renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            profiling::duration_ms(renderer_t0, renderer_t1));
        session->full_graph_session->renderer->counters()->setup_graph_parsing_wall_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }
    // 06 R3b — `create_renderer` returns `std::shared_ptr<SoftwareRenderer>`
    // (the CLI-side type contract is now SoftwareRenderer-direct).  No
    // dynamic_cast required; the renderer pointer IS the right type.
    SoftwareRenderer* sw_renderer = session->full_graph_session
        ? session->full_graph_session->renderer.get() : nullptr;
    if (session->full_graph_session && sw_renderer) {
        session->full_graph_session->surface_backend = &sw_renderer->backend();
        session->full_graph_session->surface_registry =
            &sw_renderer->runtime().surface_registry();
    }
    session->startup_ms = profiling::duration_ms(process_start_time(), renderer_t1);
    session->startup_breakdown.renderer_runtime_init_ms = session->engine_init_ms;
#ifdef CHRONON3D_ENABLE_VULKAN
    if (session->full_graph_session && session->full_graph_session->renderer) {
      if (auto* vk_b = dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(&session->full_graph_session->renderer->backend())) {
        session->startup_breakdown.vulkan_instance_ms = vk_b->init_instance_ms();
        session->startup_breakdown.vulkan_device_ms = vk_b->init_device_ms();
        session->startup_breakdown.vulkan_pipelines_ms = vk_b->init_pipelines_ms();
      }
    }
#endif

    const auto prep_start = profiling::now();

    // ── Font preflight (P0 video/text — Fase 1) ────────────────────────────
    // Check fonts referenced by the composition before rendering starts.
    // Missing fonts fail early with a clear error instead of crashing or
    // producing black frames.
    if (!session->direct_yuv_selected()) {
        const auto font_t0 = profiling::now();
        const auto preparation = runtime::prepare_render(
            sw_renderer, compiled,
            runtime::RenderPreparationOptions{.warmup_renderer = false});
        session->prepare_breakdown.font_preflight_ms = profiling::duration_ms(font_t0, profiling::now());
        if (!preparation.ok()) {
            spdlog::error("[video] Render preparation FAILED:\n{}",
                          preparation.diagnostic());
            return session;
        }
        session->prepare_timings.accumulate(preparation.timings);
    } else {
        session->prepare_breakdown.font_preflight_ms = 0.0;
        spdlog::info("[direct-yuv] bypassed generic runtime::prepare_render and font preflight");
    }

    // ── Wire counters into encoder so async converter thread can report telemetry ──
    if (sw_renderer && sw_renderer->counters()) {
        session->encoder->set_counters(sw_renderer->counters());

        // Record the sink type in telemetry counters (renderer must exist first)
        sw_renderer->counters()->video_sink_type_id.store(
            static_cast<uint64_t>(session->opts.sink.sink_type), std::memory_order_relaxed);
    }

    // ── Arena ──────────────────────────────────────────────────────────────
    // Note: the queue itself is owned by PipeExportSession (constructed above
    // with kArenaPoolCount as capacity).  We only allocate the arena pool here.
    if (!session->direct_yuv_selected()) {
        const auto arena_t0 = profiling::now();
        const size_t arena_size = compute_pipe_arena_size(
            compiled.composition->width(),
            compiled.composition->height());
        session->full_graph_session->triple_arena =
            std::make_unique<TripleBufferArena>(kArenaPoolCount, arena_size);
        session->prepare_breakdown.triple_arena_alloc_ms =
            profiling::duration_ms(arena_t0, profiling::now());
    }

    // ── Writer thread (context stored in session so it outlives the thread) ─
        auto writer_ctx = std::unique_ptr<WriterThreadContext>(
        new WriterThreadContext{
            .queue = session->queue,
            .writer_failed = session->writer_failed,
            .triple_arena = session->full_graph_session
                ? session->full_graph_session->triple_arena.get() : nullptr,
            .encoder = *session->encoder,
            .renderer = sw_renderer,
            .counters = sw_renderer ? sw_renderer->counters() : &session->direct_yuv_session->counters,
            .hot_path_mode = opts.gpu_hot_path_mode,
            .writer_encode_us_total = session->writer_encode_us_total,
            .frames_encoded = session->frames_encoded,
            .require_native_gpu =
                opts.backend_preference == graph::BackendPreference::GPU &&
                opts.encoder.encoder_backend == "native" &&
                opts.encoder.hardware_encoder == "nvenc" &&
                opts.gpu_hot_path_mode != GpuHotPathMode::Auto,
            .frame_encoder_telemetry = session->frame_encoder_telemetry,
            .trace_job_id = session->trace_job_id,
        });
    const auto spawn_t0 = profiling::now();
    session->writer_thread = std::thread(run_writer_thread, std::ref(*writer_ctx));
    session->writer_ctx = std::move(writer_ctx);
    session->prepare_breakdown.writer_thread_spawn_ms = profiling::duration_ms(spawn_t0, profiling::now());
    session->setup_prepare_ms = profiling::duration_ms(prep_start, profiling::now());

    return session;
}

RenderLoopOutput run_pipe_export_loop(
    PipeExportSession& session,
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    // Production video-frame decoder: video source layers (VideoNode) consume
    // media::MediaFrameProvider; without a live decoder every video layer
    // (light leaks, VIDEO_BACKGROUND, …) renders as an empty black framebuffer.
    // The decoder is created per render and lazily opens one session per
    // source path. NativeVideoFrameDecoder compiles to a null-decoding stub
    // when CHRONON3D_ENABLE_NATIVE_FFMPEG is off.
    session.native_decoder = std::make_shared<::chronon3d::media::NativeVideoFrameDecoder>();
    auto& native_decoder = session.native_decoder;
    native_decoder->set_counters(session.renderer_ptr()
        ? session.renderer_ptr()->counters() : &session.direct_yuv_session->counters);
    native_decoder->set_gpu_hot_path_mode(session.renderer_ptr()
        ? session.renderer_ptr()->config().gpu_hot_path_mode() : opts.gpu_hot_path_mode);
    native_decoder->set_video_runtime(session.device_runtime);
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (session.direct_yuv_selected()) {
        spdlog::info("[direct-yuv] decoder bound to the shared video device runtime");
#if defined(CHRONON3D_ENABLE_VULKAN)
    } else if (auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&session.renderer_ptr()->backend())) {
        // The encoder is opened before this loop. Use the context that owns
        // the FFmpeg/NVDEC/NVENC hardware frames; sampling the current
        // context here can select Vulkan's primary context instead.
        auto gpu = session.device_runtime ? session.device_runtime->gpu() : nullptr;
        CUcontext cuda_context = gpu
            ? reinterpret_cast<CUcontext>(gpu->native_context_handle()) : nullptr;
        if (!cuda_context) {
            spdlog::error("[video] FAIL_CLOSED: shared video runtime has no CUDA context");
        } else {
            auto importer = media::create_native_frame_importer_for_backend(
                *vulkan, session.renderer_ptr()->runtime().surface_registry(), cuda_context);
            if (importer) native_decoder->set_native_frame_importer(std::move(importer));
        }
#endif
    }
#endif
    native_decoder->set_trace_job_id(session.trace_job_id);
    media::MediaFrameProvider* video_decoder = native_decoder.get();

    RenderLoopOutput output;
    if (session.direct_yuv_selected()) {
        output = run_direct_yuv_loop(session, *native_decoder, start, end, opts);
    } else {
        // Reuse the renderer/runtime's canonical NodeCache instead of
        // creating a second local cache. This branch is FullGraph-only.
        cache::NodeCache& node_cache = session.renderer_ptr()->node_cache();
        std::vector<chronon3d::telemetry::FrameTelemetry> telemetry_frames;
        telemetry_frames.reserve(session.total_frames > 0
            ? static_cast<size_t>(session.total_frames) : 0);
        const auto render_t0 = profiling::now();

        RenderLoopContext loop_ctx{
        // 06 R3b boundary refactor: `SoftwareRenderer` no longer derives
        // from `graph::RenderBackend` — the backend is reachable via the
        // `->backend()` accessor (a domain-aware forwarder into the
        // runtime-owned backend slot, NOT an implicit IS-A upcast).
        .backend = session.renderer_ptr()->backend(),
        .node_cache = node_cache,
        .settings = settings,
        .registry = registry,
        .video_decoder = video_decoder,
        .compiled = compiled,
        .start = start,
        .end = end,
        .opts = opts,
        .sw_renderer = session.renderer_ptr(),
        .queue = session.queue,
        .writer_failed = session.writer_failed,
        .frames_encoded = session.frames_encoded,
        .execution_slots = session.full_graph_session->execution_slots,
        .triple_arena = session.full_graph_session->triple_arena.get(),
        .counters = session.renderer_ptr()->counters(),
        .telemetry_frames = telemetry_frames,
        .trace_job_id = session.trace_job_id,
        };
        output.loop_result = run_render_loop(loop_ctx);
        const auto render_t1 = profiling::now();
        output.telemetry_frames = std::move(telemetry_frames);
        output.render_ms = profiling::duration_ms(render_t0, render_t1);
        output.render_end = render_t1;
    }
    auto& loop_result = output.loop_result;

    // Close the queue to unblock the writer, then join.
    session.queue.close();
    if (session.writer_thread.joinable()) {
        session.writer_thread.join();
    }
    spdlog::info("[video] writer join complete");
    // Destroy decoder CUDA/Vulkan imports before any backend/pool cleanup.
    // The imported image must outlive its CUDA external memory bridge.
    if (session.renderer_ptr() && session.renderer_ptr()->counters()) {
    }

    if (session.writer_failed.load()) {
        loop_result.status.success = false;
        loop_result.status.writer_error = true;
    }

    // Apply the configured post-job policy. In warm daemon mode this keeps
    // the framebuffer working set alive; single-shot jobs can still select
    // TrimAfterJob to release memory explicitly.
    if (session.renderer_ptr() && session.renderer_ptr()->framebuffer_pool()) {
        spdlog::info("[video] trimming framebuffer pool");
        const auto policy = session.renderer_ptr()->framebuffer_pool()->clear_policy();
        session.renderer_ptr()->framebuffer_pool()->trim_after_job();
        if (policy == cache::FramebufferPoolClearPolicy::TrimAfterJob) {
            spdlog::info("[video] Released framebuffer pool — memory trimmed");
        } else {
            spdlog::debug("[video] Retained framebuffer pool — warm policy active");
        }
    }
    spdlog::info("[video] frame-loop cleanup complete");

    // The video IPC path does not pass through finalize_render_job(). Reclaim
    // orphaned FrameTransient Vulkan images here, after the writer has joined
    // and the final output readback is complete, while preserving warm
    // JobPersistent asset/font surfaces for the next daemon job.
    const bool native_encoder = session.opts.encoder.encoder_backend == "native";
    if (session.renderer_ptr() && !native_encoder) {
        auto& rt = session.renderer_ptr()->runtime();
        rt.backend().release_frame_transient_surfaces();
        for (const auto handle : rt.surface_registry().handles_with_lifetime(
                 runtime::LifetimeClass::FrameTransient)) {
            (void)rt.surface_registry().release(handle);
        }
        session.full_graph_session->execution_slots.close();
    }

    return output;
}

namespace {

// Text composition warm-up bundles — pre-allocates size classes used by the
// MinimalistText family so the first frames don't stall on allocation and
// pool exact-hit rate climbs from ~55% to >80% on text-heavy pipelines.
//
//   1920x900  — text-bbox ROI with glow padding (cinem-white radius ~50px).
//               Used by `apply_downsample_blur` clip-bounded regions and by
//               the GlowPipeline ROI accumulator in `build_glow_accumulator`.
//   480x270   — downsample-half heuristic at 4× scale (1920/4 × 1080/4).
//               Used by `BlurStrategy::DownsampleQuarter` for radius > 24.
//
// Both are best-fit reuse candidates when the geometric bbox is small (e.g.
// "FADE UP" centered in a 1920×1080 canvas → ROI is ~1920×900 with side
// margins). Pre-warming them gives exact-hit + best-fit reuse instead of
// fresh allocations on the hot EffectStack path.
void warmup_text_size_classes(cache::FramebufferPool& pool) {
    struct TextSizeClass { int w; int h; size_t count; const char* label; };
    // Counts tuned against the FramebufferPool default budget (384 MB) so the
    // total preallocation stays well under the cap. Color is float4 = 16 B/px,
    // so e.g. the canvas bucket (1920×1152) costs ~35 MB per buffer. The
    // chosen counts deliver ~273 MB pre-warmed and leave headroom for free
    // allocations during the actual render.
    const TextSizeClass layout[] = {
        {.w = 1920, .h = 900,  .count = 3, .label = "text-bbox+glow-pad"},
        {.w = 960,  .h = 540,  .count = 3, .label = "downsample-half"},
        {.w = 480,  .h = 270,  .count = 3, .label = "downsample-quarter"},
    };
    for (const auto& cls : layout) {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(cls.w, cls.h);
        const auto n = pool.preallocate(cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = cls.count,
            .clear = true,
            .touch_memory = false,
        });
        if (n > 0) {
            spdlog::info("[pool-warm] Pre-allocated {} buffers ({}) bucket {}x{} at startup",
                         n, cls.label, bw, bh);
        }
    }
}

} // namespace

void warmup_pipe_pool(PipeExportSession& session) {
    if (!session.renderer_ptr() || !session.renderer_ptr()->framebuffer_pool()) {
        return;
    }

    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(
        session.canvas_width, session.canvas_height);
    const auto prealloced = session.renderer_ptr()->framebuffer_pool()->preallocate(
        cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = 4,
            .clear = true,
            .touch_memory = false,
        });
    if (prealloced > 0) {
        spdlog::info("[pool-warm] Pre-allocated {} canvas buffers ({}x{} bucket) at startup",
                     prealloced, bw, bh);
    }

    // Pre-warm the text-composition ROI + downsample size classes.
    warmup_text_size_classes(*session.renderer_ptr()->framebuffer_pool());
}

} // namespace chronon3d::cli
