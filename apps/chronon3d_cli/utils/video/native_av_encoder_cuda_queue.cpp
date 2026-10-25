#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <mutex>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
bool NativeAvEncoder::drain_ready_cuda_frames(bool wait_for_one) {
    // Every caller holds mutex_: this helper operates on the encoder's shared
    // CUDA queue and deliberately does not acquire it a second time.
    if (!cuda_context_ || cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_)) != CUDA_SUCCESS) {
        spdlog::error("[native_av] failed to activate CUDA context before event drain");
        return false;
    }
    while (!pending_cuda_frames_.empty()) {
        auto& pending = pending_cuda_frames_.front();
        CUcontext cur_ctx = nullptr;
        cuCtxGetCurrent(&cur_ctx);
        const CUresult ready = cuda::query(pending.ready);
        if (ready == CUDA_ERROR_NOT_READY) {
            if (!wait_for_one) return true;
            const auto wait_t0 = Clock::now();
            const CUresult sync_result = cuda::synchronize(pending.ready);
            if (sync_result != CUDA_SUCCESS) {
                const char* name = nullptr;
                const char* text = nullptr;
                cuGetErrorName(sync_result, &name);
                cuGetErrorString(sync_result, &text);
                spdlog::error("[native_av] CUDA encode event wait failed: {} ({}) event={} owner_ctx={} cur_ctx={}",
                              name ? name : "unknown", text ? text : "unknown",
                              static_cast<void*>(pending.ready.event),
                              static_cast<void*>(pending.ready.owner_context),
                              static_cast<void*>(cur_ctx));
                return false;
            }
            const auto wait_ms = elapsed_ms(wait_t0);
            native_backpressure_ms_ += wait_ms;
            encoder_queue_backpressure_wait_ms_ += wait_ms;
            ++cuda_backpressure_wait_count_;
            if (counters_) {
                counters_->cuda_encode_event_wait_count.fetch_add(1, std::memory_order_relaxed);
                counters_->cuda_encode_event_wait_us.fetch_add(
                    static_cast<std::uint64_t>(wait_ms * 1000.0), std::memory_order_relaxed);
            }
        } else if (ready != CUDA_SUCCESS) {
            const char* name = nullptr;
            const char* text = nullptr;
            cuGetErrorName(ready, &name);
            cuGetErrorString(ready, &text);
            spdlog::error("[native_av] CUDA encode event query failed: {} ({}) event={} owner_ctx={} cur_ctx={}",
                          name ? name : "unknown", text ? text : "unknown",
                          static_cast<void*>(pending.ready.event),
                          static_cast<void*>(pending.ready.owner_context),
                          static_cast<void*>(cur_ctx));
            return false;
        }

        // Cross-item session restart: the first frame of a new container must
        // be an IDR. The frame is reused across writes, so every send states
        // the pict type explicitly (same rule as write_frame_impl).
        if (pending.frame) {
            const auto frame_pts = pending.frame->pts;
            // A queued CUDA surface is a newly rendered frame, so it ends any
            // preceding packet-duplication run. The host-frame endpoint clears
            // this flag in write_frame(), but FullGraph NVENC submits through
            // this queue instead. Leaving it set made close() treat real CUDA
            // frames after a static run as duplicate samples and append old
            // anchor packets after their DTS had already reached the mux.
            duplicate_run_active_ = false;
            const bool segment_start = segmented_mux_ && std::any_of(
                options_.output_segments.begin(), options_.output_segments.end(),
                [frame_pts](const auto& segment) { return segment.start_pts == frame_pts; });
            pending.frame->pict_type = (session_restart_intra_pending_ || segment_start)
                ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        }
        const auto send_t0 = Clock::now();
        const int send_ret = avcodec_send_frame(codec_, pending.frame);
        const double send_dur = elapsed_ms(send_t0);
        if (send_ret == 0) session_restart_intra_pending_ = false;
        native_send_frame_ms_ += send_dur;
        encoder_nvenc_submit_ms_ += send_dur;
        const int64_t popped_pts = pending.frame ? pending.frame->pts : -1;
        (void)popped_pts;
        // P1: remember the submitted content for run-anchor seeding (the
        // duplicate proof guarantees identical pixels; a buffer ref is enough).
        if (pending.frame) {
            duplicate_seed_source_.reset(av_frame_clone(pending.frame));
        }
        av_frame_unref(pending.frame);
        reusable_cuda_frames_.push_back(pending.frame);
        pending.frame = nullptr;
        cuda::destroy(pending.ready);
        pending_cuda_frames_.pop_front();
        if (send_ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(send_ret, error, sizeof(error));
            spdlog::error("[native_av] avcodec_send_frame failed for GPU frame {}: {}",
                          frames_written_, error);
            return false;
        }
        ++codec_frames_submitted_;
        const auto drain_t0 = Clock::now();
        if (!drain_packets()) {
            spdlog::error("[native_av] NVENC packet drain failed after GPU frame {}",
                          frames_written_);
            return false;
        }
        encoder_packet_drain_ms_ += elapsed_ms(drain_t0);
        ++frames_written_;
        wait_for_one = false;
    }
    return true;
}
#endif

} // namespace chronon3d::cli
