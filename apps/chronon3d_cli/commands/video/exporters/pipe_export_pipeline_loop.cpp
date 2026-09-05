#include "../common/pipe_export_pipeline.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/native_frame_importer_factory.hpp>
#include <chronon3d/media/video/detail/video_execution_legacy.hpp>
#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <cuda.h>
#include <libavutil/buffer.h>
#endif
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

namespace chronon3d::cli {

RenderLoopOutput run_pipe_export_loop(
    PipeExportSession& session,
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    RenderLoopOutput output;
    bool native_video_setup_failed = false;
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
        // VideoDeviceRuntime is intentionally lazy.  Materialize its FFmpeg
        // hwdevice before reading GpuRuntime::native_context_handle(); the
        // latter is null until the shared primary CUDA context has been
        // initialized.  Without this ordering the native importer is skipped
        // and the render graph fails later while seeding the destination.
        AVBufferRef* cuda_hwdevice = session.device_runtime
            ? session.device_runtime->ref_cuda_hwdevice() : nullptr;
        auto gpu = session.device_runtime ? session.device_runtime->gpu() : nullptr;
        CUcontext cuda_context = gpu
            ? reinterpret_cast<CUcontext>(gpu->native_context_handle()) : nullptr;
        if (!cuda_hwdevice || !cuda_context) {
            spdlog::error(
                "[video] FAIL_CLOSED: shared video runtime could not initialize "
                "the CUDA primary context before Vulkan importer setup");
            native_video_setup_failed = true;
        } else {
            auto importer = media::create_native_frame_importer_for_backend(
                *vulkan, session.renderer_ptr()->runtime().surface_registry(), cuda_context);
            if (importer) {
                native_decoder->set_native_frame_importer(std::move(importer));
            } else {
                spdlog::error(
                    "[video] FAIL_CLOSED: could not create the Vulkan native frame importer");
                native_video_setup_failed = true;
            }
        }
        if (cuda_hwdevice) av_buffer_unref(&cuda_hwdevice);
#endif
    }
#endif
    native_decoder->set_trace_job_id(session.trace_job_id);
    media::MediaFrameProvider* video_decoder = native_decoder.get();

    if (native_video_setup_failed) {
        mark_pipe_render_failed(output.loop_result.status, start);
    } else if (session.direct_yuv_selected()) {
        output = run_direct_yuv_loop(session, *native_decoder, start, end, opts);
    } else {
        cache::NodeCache& node_cache = session.renderer_ptr()->node_cache();
        std::vector<chronon3d::telemetry::FrameTelemetry> telemetry_frames;
        telemetry_frames.reserve(session.total_frames > 0
            ? static_cast<size_t>(session.total_frames) : 0);
        const auto render_t0 = profiling::now();

        RenderLoopContext loop_ctx{
        .backend = session.renderer_ptr()->backend(),
        .node_cache = node_cache,
        .settings = settings,
        .registry = registry,
        .video_decoder = video_decoder,
        .compiled = compiled,
        .start = start,
        .end = end,
        .opts = opts,
        .execution_plan = session.execution_plan,
        .sw_renderer = session.renderer_ptr(),
        .queue = session.queue,
        .writer_failed = session.writer_failed,
        .frames_encoded = session.frames_encoded,
        .execution_slots = session.full_graph_session->execution_slots,
        .device_runtime = session.device_runtime,
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

    session.queue.close();
    if (session.writer_thread.joinable()) session.writer_thread.join();
    spdlog::info("[video] writer join complete");
    if (session.full_graph_session) {
        // B7 drain: retired slots stay pinned until their encoder completion
        // fires; reap them (or force-close on failure) before teardown touches
        // any surface the encoder may still reference.
        session.full_graph_session->execution_slots.drain();
    }

    if (session.writer_failed.load()) {
        loop_result.status.success = false;
        loop_result.status.writer_error = true;
    }

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

} // namespace chronon3d::cli
