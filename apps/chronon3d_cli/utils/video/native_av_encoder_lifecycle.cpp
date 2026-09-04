#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <mutex>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

bool NativeAvEncoder::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!codec_) {
        if (!open_complete_) abort_open();
        return true;
    }
    open_complete_ = false;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    spdlog::info("[native_av] close: draining CUDA pending={}", pending_cuda_frames_.size());
    if (gpu_nvenc_ && !drain_ready_cuda_frames(true)) {
        spdlog::error("[native_av] failed to drain pending CUDA frames");
    }
    spdlog::info("[native_av] close: CUDA drain complete");
#endif

    const auto t_flush0 = Clock::now();
    spdlog::info("[native_av] close: sending encoder flush");
    avcodec_send_frame(codec_, nullptr);
    spdlog::info("[native_av] close: encoder flush sent");

    drain_packets();
    spdlog::info("[native_av] close: packets drained");
    if (audio_input_ && !mux_source_audio()) {
        spdlog::error("[native_av] failed to mux source audio");
    }
    const double flush_ms = elapsed_ms(t_flush0);
    native_flush_ms_ += flush_ms;

    const auto t_trailer0 = Clock::now();
    if (mux_) {
        spdlog::info("[native_av] close: finalizing mux session");
        (void)mux_->finalize();
        spdlog::info("[native_av] close: mux session finalized");
    }
    const double trailer_ms = elapsed_ms(t_trailer0);
    native_trailer_ms_ += trailer_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->encoder_flush_wall_ms.fetch_add(
            static_cast<uint64_t>(flush_ms), std::memory_order_relaxed);
        profiling::g_current_counters->mux_finalize_wall_ms.fetch_add(
            static_cast<uint64_t>(trailer_ms), std::memory_order_relaxed);
    }

    spdlog::info("[native_av] Closed native encoder — {} frames written, YUV cache: {} hits / {} misses",
                 frames_written_, cache_hits_, cache_misses_);
    return true;
}

NativeAvEncoder::~NativeAvEncoder() {
    shutdown_noexcept();
}

void NativeAvEncoder::shutdown_noexcept() noexcept {
    if (open_complete_) {
        try {
            (void)close();
        } catch (...) {
            spdlog::error("[native_av] exception during destructor close; forcing abort cleanup");
            abort_open();
        }
    } else {
        abort_open();
    }
}

void NativeAvEncoder::abort_open() noexcept {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (auto& pending : pending_cuda_frames_) {
        if (pending.frame) av_frame_free(&pending.frame);
        cuda::destroy(pending.ready);
    }
    pending_cuda_frames_.clear();
    for (auto* reusable : reusable_cuda_frames_) {
        av_frame_free(&reusable);
    }
    reusable_cuda_frames_.clear();
    if (cuda_context_) (void)cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_));
#ifdef CHRONON3D_ENABLE_VULKAN
    cuda_nv12_compositors_.clear();
#endif
    if (cuda_stream_) {
        (void)cuStreamDestroy(cuda_stream_);
        cuda_stream_ = nullptr;
    }
#endif
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&codec_);
    avformat_close_input(&audio_input_);
    audio_input_stream_ = -1;
    mux_.reset();
    packet_ = nullptr;
    frame_ = nullptr;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    av_buffer_unref(&cuda_frames_ref_);
    av_buffer_unref(&cuda_device_ref_);
    cuda_context_ = nullptr;
    gpu_nvenc_ = false;
#endif
    open_complete_ = false;
}

} // namespace chronon3d::cli
