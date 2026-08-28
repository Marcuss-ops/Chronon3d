#include "pipe_export_session.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <vector>

namespace chronon3d::cli {

// The writer owns the encoder-facing side of the queue. Keeping it separate
// from the render loop makes the GPU-surface lifetime and fallback policy
// auditable without mixing them with frame evaluation.
void run_writer_thread(const WriterThreadContext& ctx) {
    profiling::g_current_counters = ctx.renderer.counters();
    profiling::g_current_framebuffer_pool = ctx.renderer.framebuffer_pool().get();
    bool arena_notified = false;
    struct DeferredInteropSlot {
        std::size_t slot{FrameInteropRing::kInvalidSlot};
        graph::RenderBackend* backend{nullptr};
        runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
    };
    std::vector<DeferredInteropSlot> deferred_slots;

    const auto retire_ready_slots = [&]() {
        deferred_slots.erase(
            std::remove_if(deferred_slots.begin(), deferred_slots.end(),
                [&](const DeferredInteropSlot& pending) {
                    if (!pending.backend || ctx.encoder.poll_native_surface(
                            *pending.backend, pending.surface)) {
                        ctx.interop_ring.release(pending.slot);
                        return true;
                    }
                    return false;
                }),
            deferred_slots.end());
    };

    for (;;) {
        // Perfetto counter tracks (pipeline health): frames waiting between
        // the render thread and the writer, and encode work still in flight
        // here (GPU surfaces the encoder has not finished consuming).
        // Guarded by TracingActive() so the mutex-guarded queue probe costs
        // nothing when no trace session is running.
        if (chronon3d::tracing::TracingActive()) {
            CHRONON_TRACE_COUNTER("chronon.pipeline", "render_queue_depth",
                static_cast<int64_t>(ctx.queue.size_approx()));
            CHRONON_TRACE_COUNTER("chronon.pipeline", "encoder_queue_depth",
                static_cast<int64_t>(deferred_slots.size()));
        }
        retire_ready_slots();
        RenderFramePackage package;
        const auto pop_t0 = profiling::now();
        const bool popped = ctx.queue.pop_for(package, std::chrono::milliseconds(1));
        const auto pop_t1 = profiling::now();
        const uint64_t dequeue_ms = static_cast<uint64_t>(
            profiling::duration_ms(pop_t0, pop_t1));

        if (!popped) {
            if (ctx.queue.closed_and_empty()) break;
            continue;
        }
        if (ctx.renderer.counters()) {
            ctx.renderer.counters()->io_queue_pop_wait_ms.fetch_add(
                dequeue_ms, std::memory_order_relaxed);
        }

        if (package.framebuffer) {
            const auto release_interop_slot = [&]() noexcept {
                ctx.interop_ring.release(package.interop_slot);
            };
            if (!arena_notified) {
                spdlog::info("[video] Exporting via Arena-backed SIMD pipeline");
                arena_notified = true;
            }

            const auto enc_t0 = profiling::now();
            const Framebuffer& fb_ref = *package.framebuffer;
            // Timeline tracing: terminating flow hop — the frame's
            // NVDEC → render chain ends here at the encoder sink. The flow id
            // is the same MakeFlowId(job, frame) emitted by decode and render.
            const auto trace_job_id = ctx.trace_job_id;
            const auto trace_flow = chronon3d::tracing::MakeFlowId(
                trace_job_id, static_cast<uint64_t>(package.frame_number));
            CHRONON_TRACE_FLOW_END_IDS("chronon.encode", "EncodeFrame",
                trace_flow, trace_job_id,
                static_cast<uint64_t>(package.frame_number));
            const bool gpu_frame =
                package.native_surface != runtime::kInvalidRenderSurfaceHandle &&
                package.backend != nullptr;
            if (gpu_frame && ctx.renderer.counters()) {
                ctx.renderer.counters()->gpu_native_surface_frames.fetch_add(
                    1, std::memory_order_relaxed);
            }
            if (!gpu_frame && ctx.require_native_gpu) {
                if (ctx.renderer.counters()) {
                    ctx.renderer.counters()->video_native_fallback_frames.fetch_add(
                        1, std::memory_order_relaxed);
                    ctx.renderer.counters()->gpu_encode_failures.fetch_add(
                        1, std::memory_order_relaxed);
                }
                spdlog::error(
                    "[video] Native GPU profile lost its Vulkan surface at frame {}; refusing CPU fallback",
                    package.frame_number);
                release_interop_slot();
                ctx.writer_failed.store(true);
                ctx.queue.close();
                return;
            }

            const bool encoded = gpu_frame
                ? (package.native_surface_ready
                    ? ctx.encoder.write_prepared_native_surface(
                        *package.backend, package.source_surface, package.native_surface)
                    : ctx.encoder.write_native_surface(
                        *package.backend, package.source_surface, package.native_surface))
                : ctx.encoder.write_frame_async(fb_ref, std::move(package.framebuffer));

            if (!encoded) {
                if (ctx.renderer.counters()) {
                    ctx.renderer.counters()->gpu_encode_failures.fetch_add(
                        1, std::memory_order_relaxed);
                }
                release_interop_slot();
                ctx.writer_failed.store(true);
                ctx.queue.close();
                return;
            }
            const bool surface_ready = !gpu_frame || ctx.encoder.poll_native_surface(
                *package.backend, package.native_surface);
            if (gpu_frame && !surface_ready) {
                deferred_slots.push_back({package.interop_slot, package.backend,
                                          package.native_surface});
            } else {
                release_interop_slot();
            }
            if (ctx.renderer.counters()) {
                if (gpu_frame) {
                    ctx.renderer.counters()->gpu_native_encode_frames.fetch_add(
                        1, std::memory_order_relaxed);
                } else {
                    ctx.renderer.counters()->video_pipe_fallback_frames.fetch_add(
                        1, std::memory_order_relaxed);
                }
            }
            ++ctx.frames_encoded;
            const auto enc_t1 = profiling::now();
            ctx.writer_encode_us_total.fetch_add(
                static_cast<uint64_t>(profiling::duration_us(enc_t0, enc_t1)),
                std::memory_order_relaxed);

            ctx.frame_encoder_telemetry.push_back({
                .frame_number = static_cast<int>(package.frame_number),
                .conversion_copy_ms = ctx.encoder.last_frame_telemetry().conversion_copy_ms,
                .pixel_format_convert_ms = ctx.encoder.last_frame_telemetry().pixel_format_convert_ms,
                .color_space_convert_ms = ctx.encoder.last_frame_telemetry().color_space_convert_ms,
                .encoder_ms = ctx.encoder.last_frame_telemetry().encoder_ms,
                .pipe_write_ms = ctx.encoder.last_frame_telemetry().pipe_write_ms,
                .backpressure_wait_ms = ctx.encoder.last_frame_telemetry().backpressure_wait_ms,
                .pipe_write_cpu_ms = ctx.encoder.last_frame_telemetry().pipe_write_cpu_ms,
                .pipe_backpressure_wait_ms = ctx.encoder.last_frame_telemetry().pipe_backpressure_wait_ms,
                .native_convert_ms = ctx.encoder.last_frame_telemetry().native_convert_ms,
                .native_send_ms = ctx.encoder.last_frame_telemetry().native_send_ms,
                .native_receive_ms = ctx.encoder.last_frame_telemetry().native_receive_ms,
                .native_mux_ms = ctx.encoder.last_frame_telemetry().native_mux_ms,
            });
        }

        ctx.triple_arena.release(package.arena);
    }

    // The queue is closed only after the producer has submitted its final
    // frame. Keep the ring slots retained until the encoder has consumed all
    // CUDA work, then release them in the same order used during production.
    spdlog::info("[video] writer queue drained; deferred native slots={}", deferred_slots.size());
    while (!deferred_slots.empty()) {
        if (!ctx.encoder.finish_native_surface(
                *deferred_slots.front().backend,
                deferred_slots.front().surface)) {
            ctx.writer_failed.store(true);
            break;
        }
        spdlog::info("[video] finished native surface slot; remaining={}", deferred_slots.size());
        retire_ready_slots();
    }
}

} // namespace chronon3d::cli
