#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <mutex>
#include <utility>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
CUresult NativeAvEncoder::acquire_recorded_cuda_event(
    cuda::OwnedCudaEvent& out, CUcontext context, CUstream stream) noexcept {
    if (!reusable_cuda_events_.empty()) {
        out = reusable_cuda_events_.back();
        reusable_cuda_events_.pop_back();
        cuda::ScopedCudaContext guard(context);
        if (!guard.ok()) {
            cuda::destroy(out);
            return CUDA_ERROR_INVALID_CONTEXT;
        }
        out.owner_context = context;
        const auto recorded = cuEventRecord(out.event, stream);
        if (recorded != CUDA_SUCCESS) cuda::destroy(out);
        return recorded;
    }
    return cuda::create_recorded(out, context, stream);
}

void NativeAvEncoder::release_cuda_event(cuda::OwnedCudaEvent&& event) noexcept {
    if (!event.event) return;
    if (reusable_cuda_events_.size() < kCudaEncodeRingSlots) {
        reusable_cuda_events_.push_back(event);
        event = {};
        return;
    }
    cuda::destroy(event);
}

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
            const bool resync = duplicate_run_active_ &&
                duplicate_run_mode_ == DuplicateRunMode::Replicate;
            duplicate_run_active_ = false;
            duplicate_run_mode_ = DuplicateRunMode::Undecided;
            last_real_frame_pts_ = static_cast<std::int64_t>(frame_pts);
            last_real_packet_seen_ = false;
            last_real_packet_was_key_ = false;
            last_real_key_packet_.reset();
            const bool segment_start = segmented_mux_ && std::any_of(
                options_.output_segments.begin(), options_.output_segments.end(),
                [frame_pts](const auto& segment) { return segment.start_pts == frame_pts; });
            pending.frame->pict_type = (resync || session_restart_intra_pending_ || segment_start)
                ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        }
        const auto send_t0 = Clock::now();
        int send_ret = avcodec_send_frame(codec_, pending.frame);
        double send_dur = elapsed_ms(send_t0);
        // P7 (TICKET-FIX-P7-NVENC-CUDA-QUEUE-EAGAIN-HANDLING-20261117):
        // AVERROR(EAGAIN) is ordinary NVENC output backpressure — the frame was
        // NOT accepted. Drain packets and retry with the SAME frame, exactly as
        // the host-frame lane does. The previous code released the frame, popped
        // it and returned false, turning backpressure into a lost frame and a
        // failed export.
        int eagain_retries = 0;
        double backpressure_ms = 0.0;
        while (send_ret == AVERROR(EAGAIN) &&
               eagain_retries < detail::kMaxCudaEagainRetries) {
            ++eagain_retries;
            const auto bp_t0 = Clock::now();
            if (!drain_packets()) {
                spdlog::error(
                    "[native_av] NVENC packet drain failed during EAGAIN on GPU frame {}",
                    frames_written_);
                return false;
            }
            backpressure_ms += elapsed_ms(bp_t0);
            const auto retry_t0 = Clock::now();
            send_ret = avcodec_send_frame(codec_, pending.frame);
            send_dur += elapsed_ms(retry_t0);
        }
        native_send_frame_ms_ += send_dur;
        encoder_nvenc_submit_ms_ += send_dur;
        native_backpressure_ms_ += backpressure_ms;
        if (send_ret == AVERROR(EAGAIN)) {
            spdlog::error(
                "[native_av] avcodec_send_frame still EAGAIN after {} retries for GPU frame {}",
                detail::kMaxCudaEagainRetries, frames_written_);
            return false;
        }
        if (send_ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(send_ret, error, sizeof(error));
            spdlog::error("[native_av] avcodec_send_frame failed for GPU frame {}: {}",
                          frames_written_, error);
            return false;
        }
        session_restart_intra_pending_ = false;
        // Only a frame the codec actually accepted may be released. The seed
        // keeps a buffer reference instead of cloning a whole AVFrame per
        // frame: the pixels are guaranteed identical to this submission.
        if (pending.frame) {
            if (!duplicate_seed_source_) {
                duplicate_seed_source_.reset(av_frame_alloc());
            }
            if (duplicate_seed_source_) {
                av_frame_unref(duplicate_seed_source_.get());
                (void)av_frame_ref(duplicate_seed_source_.get(), pending.frame);
            }
        }
        av_frame_unref(pending.frame);
        reusable_cuda_frames_.push_back(pending.frame);
        pending.frame = nullptr;
        release_cuda_event(std::move(pending.ready));
        pending_cuda_frames_.pop_front();
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
