#include "pipe_export_session.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace chronon3d::cli {

namespace {

double anchor_offset_ms(const EncoderSinkContext& ctx,
                        std::chrono::steady_clock::time_point point) noexcept {
    const auto anchor_ns = ctx.telemetry_anchor_ns.load(std::memory_order_acquire);
    if (anchor_ns == 0 || point == std::chrono::steady_clock::time_point{}) {
        return 0.0;
    }
    const auto point_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        point.time_since_epoch()).count();
    return static_cast<double>(point_ns - anchor_ns) / 1'000'000.0;
}

double queue_wait_ms(std::chrono::steady_clock::time_point enqueue_start,
                     std::chrono::steady_clock::time_point dequeue_start) noexcept {
    if (enqueue_start == std::chrono::steady_clock::time_point{}) return 0.0;
    return std::max(0.0, std::chrono::duration<double, std::milli>(
        dequeue_start - enqueue_start).count());
}

class EncoderGpuCompletion final : public runtime::GpuCompletion {
public:
    EncoderGpuCompletion(IVideoEncoder& encoder, graph::RenderBackend& backend,
                         runtime::RenderSurfaceHandle surface) noexcept
        : encoder_(encoder), backend_(backend), surface_(surface) {}

    [[nodiscard]] bool ready() const noexcept override {
        return encoder_.poll_native_surface(backend_, surface_);
    }

    void wait() override {
        (void)encoder_.finish_native_surface(backend_, surface_);
    }

private:
    IVideoEncoder& encoder_;
    graph::RenderBackend& backend_;
    runtime::RenderSurfaceHandle surface_;
};

} // namespace

// The writer owns the encoder-facing side of the queue. Keeping it separate
// from the render loop makes the GPU-surface lifetime and fallback policy
// auditable without mixing them with frame evaluation.
//
// Wave 3 — the handoff is the fixed-capacity SPSC RenderFrameQueue
// (BoundedSpscRing data path, explicit Open/Closed/Failed lifecycle). The
// render loop is the single producer; this thread is the single consumer.
// Blocking is producer-side backpressure (retry-until-slot) plus this
// consumer's ~1 ms pop_for cadence, which doubles as the interop-poll tick.

// F5.2 — single exception boundary for the writer thread. An exception that
// escapes a std::thread's callable calls std::terminate and kills the whole
// process OUTSIDE the fail-closed finalize path (no .partial removal, no
// structured error, meaningless exit code). The boundary must publish the
// failure through the SAME channel the encoder error paths already use
// (writer_failed + queue.fail()) so the render loop observes it per-frame,
// finalize refuses to publish, and the .partial artifact is removed. It must
// NOT swallow silently: the reason is logged with the failing frame when the
// package is identifiable.
void run_encoder_sink_thread(const EncoderSinkContext& ctx) {
    // Arena release for the in-flight package is handled by the impl loop:
    // every early-return inside it releases cpu_arena before propagating, so
    // the boundary here only needs to publish failure + close the queue.
    try {
        run_encoder_sink_thread_impl(ctx);
    } catch (const std::exception& e) {
        spdlog::error("[video] writer thread exception: {}", e.what());
        ctx.writer_failed.store(true);
        ctx.queue.fail();
    } catch (...) {
        spdlog::error("[video] writer thread exception: unknown");
        ctx.writer_failed.store(true);
        ctx.queue.fail();
    }
}

void run_encoder_sink_thread_impl(const EncoderSinkContext& ctx) {
    profiling::g_current_counters = ctx.counters;
    profiling::g_current_framebuffer_pool = ctx.renderer
        ? ctx.renderer->framebuffer_pool().get() : nullptr;
    bool arena_notified = false;
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
                static_cast<int64_t>(ctx.queue.size_approx()));
        }
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
        if (ctx.counters) {
            ctx.counters->io_queue_pop_wait_ms.fetch_add(
                dequeue_ms, std::memory_order_relaxed);
        }

        // The popped package is already in the encoder's critical section;
        // include it alongside the remaining queue backlog.
        const auto encode_in_flight = static_cast<std::uint64_t>(
            ctx.queue.size_approx()) + 1;
        observe_pipeline_peak(
            ctx.pipeline_metrics.encode_frames_in_flight_peak,
            encode_in_flight);

        if (const auto* direct = std::get_if<DirectYuvFramePackage>(&package)) {
            const auto enc_t0 = profiling::now();
            if (ctx.counters) {
                ctx.counters->gpu_native_surface_frames.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const bool encoded = ctx.encoder.write_direct_yuv(*direct->direct_yuv);
            const auto enc_t1 = profiling::now();
            ctx.writer_encode_us_total.fetch_add(
                static_cast<uint64_t>(profiling::duration_ms(enc_t0, enc_t1) * 1000.0),
                std::memory_order_relaxed);
            if (!encoded) {
                spdlog::error("[video] Direct-YUV encoder rejected frame {}",
                              static_cast<int>(direct->frame_number));
                if (ctx.counters) {
                    ctx.counters->gpu_encode_failures.fetch_add(
                        1, std::memory_order_relaxed);
                }
                ctx.writer_failed.store(true);
                ctx.queue.fail();
                return;
            }
            if (ctx.counters) {
                ctx.counters->gpu_native_encode_frames.fetch_add(
                    1, std::memory_order_relaxed);
                ctx.counters->nvenc_frames.fetch_add(
                    1, std::memory_order_relaxed);
            }
            ++ctx.frames_encoded;
            const auto direct_telemetry = ctx.encoder.last_frame_telemetry();
            ctx.frame_encoder_telemetry.push_back({
                .frame_number = static_cast<int>(direct->frame_number),
                .encoder_ms = direct_telemetry.encoder_ms,
                .native_send_ms = direct_telemetry.native_send_ms,
                .native_receive_ms = direct_telemetry.native_receive_ms,
                .native_mux_ms = direct_telemetry.native_mux_ms,
                .encoder_wall_start_ms = anchor_offset_ms(ctx, enc_t0),
                .encoder_wall_end_ms = anchor_offset_ms(ctx, enc_t1),
                .writer_queue_wait_ms = queue_wait_ms(
                    direct->enqueued_at, enc_t0),
                .writer_encode_wall_ms = std::chrono::duration<double, std::milli>(
                    enc_t1 - enc_t0).count(),
            });
            continue;
        }

        auto* full = std::get_if<FullGraphFramePackage>(&package);
        if (full && full->cpu_fallback) {
            if (!arena_notified) {
                spdlog::info("[video] Exporting via Arena-backed SIMD pipeline");
                arena_notified = true;
            }

            const auto enc_t0 = profiling::now();
            const Framebuffer& fb_ref = *full->cpu_fallback;
            // Timeline tracing: terminating flow hop — the frame's
            // NVDEC → render chain ends here at the encoder sink. The flow id
            // is the same MakeFlowId(job, frame) emitted by decode and render.
            const auto trace_job_id = ctx.trace_job_id;
            const auto trace_flow = chronon3d::tracing::MakeFlowId(
                trace_job_id, static_cast<uint64_t>(full->frame_number));
            CHRONON_TRACE_FLOW_END_IDS("chronon.encode", "EncodeFrame",
                trace_flow, trace_job_id,
                static_cast<uint64_t>(full->frame_number));
            auto* slot = full->slot.valid() ? &full->slot.slot() : nullptr;
            const bool gpu_frame = slot &&
                slot->native_surface != runtime::kInvalidRenderSurfaceHandle &&
                slot->backend != nullptr;
            if (gpu_frame && ctx.counters) {
                ctx.counters->gpu_native_surface_frames.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const bool plan_requires_native = ctx.execution_plan &&
                ctx.execution_plan->encode == media::EncodePath::Nvenc &&
                ctx.execution_plan->interop != media::InteropPath::None;
            if (!gpu_frame && (ctx.require_native_gpu || plan_requires_native ||
                               ctx.hot_path_mode == GpuHotPathMode::RequireGpuNative ||
                               ctx.hot_path_mode == GpuHotPathMode::RequireDirectYuv)) {
                if (ctx.counters) {
                    ctx.counters->video_native_fallback_frames.fetch_add(
                        1, std::memory_order_relaxed);
                    ctx.counters->gpu_encode_failures.fetch_add(
                        1, std::memory_order_relaxed);
                }
                spdlog::error(
                    "[video] GPU_NATIVE_REQUIRED: non-GPU surface at frame {}; refusing CPU fallback",
                    full->frame_number);
                // No encoder contact happened for this slot: the lease
                // destructor performs the guarded Encoding→Free recycle.
                ctx.writer_failed.store(true);
                ctx.queue.fail();
                return;
            }

            const bool encoded = gpu_frame
                ? (slot->native_surface_prepared()
                    ? ctx.encoder.write_prepared_native_surface(
                        *slot->backend, slot->source_surface, slot->native_surface,
                        full->dirty_regions)
                    : ctx.encoder.write_native_surface(
                        *slot->backend, slot->source_surface, slot->native_surface,
                        full->dirty_regions))
                : ctx.encoder.write_frame_async(fb_ref, std::move(full->cpu_fallback));

            if (!encoded) {
                if (ctx.counters) {
                    ctx.counters->gpu_encode_failures.fetch_add(
                        1, std::memory_order_relaxed);
                }
                if (gpu_frame) {
                    // B7: the encoder may have partially consumed the native
                    // surface before rejecting the frame. Pin the slot under a
                    // completion token and let the reaper recycle it only once
                    // poll_native_surface reports the encoder is done with it.
                    full->slot.retire(std::make_shared<EncoderGpuCompletion>(
                        ctx.encoder, *slot->backend, slot->native_surface));
                }
                ctx.writer_failed.store(true);
                ctx.queue.fail();
                return;
            }
            if (gpu_frame && !slot->transition_interop_state(
                    runtime::InteropFrameState::EncodeSubmitted)) {
                spdlog::error(
                    "[video] invalid encode submission lifecycle for slot {}",
                    slot->slot_id);
                // B7: same pin-until-completion discipline as above — the
                // encoder owns the surface even though bookkeeping failed.
                full->slot.retire(std::make_shared<EncoderGpuCompletion>(
                    ctx.encoder, *slot->backend, slot->native_surface));
                ctx.writer_failed.store(true);
                ctx.queue.fail();
                return;
            }
            const bool surface_ready = !gpu_frame || ctx.encoder.poll_native_surface(
                *slot->backend, slot->native_surface);
            if (gpu_frame && !surface_ready) {
                full->slot.retire(std::make_shared<EncoderGpuCompletion>(
                    ctx.encoder, *slot->backend, slot->native_surface));
            } else {
                if (gpu_frame) {
                    if (!slot->transition_interop_state(
                            runtime::InteropFrameState::EncodeConsumed)) {
                        spdlog::error(
                            "[video] invalid encode completion lifecycle for slot {}",
                            slot->slot_id);
                        // Surface already reported ready, but keep the same
                        // invariant: recycle only through the guarded path.
                        full->slot.retire(std::make_shared<EncoderGpuCompletion>(
                            ctx.encoder, *slot->backend, slot->native_surface));
                        ctx.writer_failed.store(true);
                        ctx.queue.fail();
                        return;
                    }
                }
                full->slot.release();
            }
            if (ctx.counters) {
                if (gpu_frame) {
                    ctx.counters->gpu_native_encode_frames.fetch_add(
                        1, std::memory_order_relaxed);
                    ctx.counters->nvenc_frames.fetch_add(
                        1, std::memory_order_relaxed);
                    ctx.counters->vulkan_frames.fetch_add(
                        1, std::memory_order_relaxed);
                } else {
                    ctx.counters->video_pipe_fallback_frames.fetch_add(
                        1, std::memory_order_relaxed);
                    ctx.counters->software_encode_frames.fetch_add(
                        1, std::memory_order_relaxed);
                }
            }
            ++ctx.frames_encoded;
            const auto enc_t1 = profiling::now();
            ctx.writer_encode_us_total.fetch_add(
                static_cast<uint64_t>(profiling::duration_us(enc_t0, enc_t1)),
                std::memory_order_relaxed);

            const auto telemetry = ctx.encoder.last_frame_telemetry();
            ctx.frame_encoder_telemetry.push_back({
                .frame_number = static_cast<int>(full->frame_number),
                .conversion_copy_ms = telemetry.conversion_copy_ms,
                .pixel_format_convert_ms = telemetry.pixel_format_convert_ms,
                .color_space_convert_ms = telemetry.color_space_convert_ms,
                .encoder_ms = telemetry.encoder_ms,
                .pipe_write_ms = telemetry.pipe_write_ms,
                .backpressure_wait_ms = telemetry.backpressure_wait_ms,
                .pipe_write_cpu_ms = telemetry.pipe_write_cpu_ms,
                .pipe_backpressure_wait_ms = telemetry.pipe_backpressure_wait_ms,
                .native_convert_ms = telemetry.native_convert_ms,
                .native_send_ms = telemetry.native_send_ms,
                .native_receive_ms = telemetry.native_receive_ms,
                .native_mux_ms = telemetry.native_mux_ms,
                .encoder_wall_start_ms = anchor_offset_ms(ctx, enc_t0),
                .encoder_wall_end_ms = anchor_offset_ms(ctx, enc_t1),
                .writer_queue_wait_ms = queue_wait_ms(
                    full->enqueued_at, enc_t0),
                .writer_encode_wall_ms = std::chrono::duration<double, std::milli>(
                    enc_t1 - enc_t0).count(),
            });
        }

        if (full && full->cpu_arena && ctx.triple_arena) {
            ctx.triple_arena->release(std::move(full->cpu_arena));
        }
    }
}

} // namespace chronon3d::cli
