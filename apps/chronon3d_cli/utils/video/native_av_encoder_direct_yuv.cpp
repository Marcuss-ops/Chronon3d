#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <exception>
#include <mutex>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

bool NativeAvEncoder::write_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination) {
    return write_native_surface_impl(backend, source, destination, false);
}

bool NativeAvEncoder::write_prepared_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination) {
    return write_native_surface_impl(backend, source, destination, true);
}

bool NativeAvEncoder::write_direct_yuv(const media::video::DirectYuvFrame& direct) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)direct;
    return false;
#else
    // Z1 typed-feed guard: DirectYuv frames belong to a DirectYUV session
    // only. A frame arriving on a FullGraph-native session would bypass the
    // graph's encode path and re-enter the CUDA queue out-of-order.
    if (!options_.direct_yuv_mode) {
        spdlog::error(
            "[native_av] DirectYuv frame feed refused on a non-DirectYUV "
            "session (typed encoder capability mismatch)");
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto direct_submit_t0 = Clock::now();
    last_frame_telemetry_ = EncoderFrameTelemetry{};
    if (!gpu_nvenc_ || !cuda_context_ || !cuda_frames_ref_ ||
        !direct.decoded || direct.decoded->format != AV_PIX_FMT_CUDA ||
        !direct.decoded->data[0] || !direct.decoded->data[1] ||
        !direct.program ||
        (direct.program->batch.instances.empty() != direct.program->resources.empty())) {
        spdlog::error("[native_av] invalid DirectCudaYuv frame contract");
        return false;
    }
    if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_)) != CUDA_SUCCESS) {
        spdlog::error("[native_av] DirectYuv failed to activate CUDA context on writer thread");
        return false;
    }
    const bool has_overlay = !direct.program->batch.instances.empty();
    if (has_overlay && !direct_yuv_compositor_) {
        try {
            direct_yuv_compositor_ = std::make_unique<media::video::CudaNv12Compositor>(
                reinterpret_cast<CUcontext>(cuda_context_));
        } catch (const std::exception& error) {
            spdlog::error("[native_av] DirectCudaYuv compositor init failed: {}", error.what());
            return false;
        }
    }
    if (!drain_ready_cuda_frames(false)) {
        spdlog::error("[native_av] DirectYuv initial CUDA queue drain failed");
        return false;
    }
    if (pending_cuda_frames_.size() >= kCudaEncodeRingSlots &&
        !drain_ready_cuda_frames(true)) {
        ++cuda_backpressure_wait_count_;
        return false;
    }

    const auto acq_t0 = Clock::now();
    AVFrame* gpu_frame = nullptr;
    if (!reusable_cuda_frames_.empty()) {
        gpu_frame = reusable_cuda_frames_.front();
        reusable_cuda_frames_.pop_front();
        av_frame_unref(gpu_frame);
    } else {
        gpu_frame = av_frame_alloc();
    }
    encoder_surface_acquire_ms_ += elapsed_ms(acq_t0);
    if (!gpu_frame) {
        spdlog::error("[native_av] DirectYuv output AVFrame allocation failed");
        return false;
    }
    gpu_frame->format = AV_PIX_FMT_CUDA;
    gpu_frame->width = options_.width;
    gpu_frame->height = options_.height;
    gpu_frame->color_range = codec_->color_range;
    gpu_frame->colorspace = codec_->colorspace;
    gpu_frame->color_trc = codec_->color_trc;
    gpu_frame->color_primaries = codec_->color_primaries;
    gpu_frame->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
    const auto hw_t0 = Clock::now();
    const int hw_rc = av_hwframe_get_buffer(cuda_frames_ref_, gpu_frame, 0);
    encoder_hwframe_get_buffer_ms_ += elapsed_ms(hw_t0);
    if (hw_rc < 0 || !gpu_frame->data[0] || !gpu_frame->data[1]) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(hw_rc, error, sizeof(error));
        spdlog::error("[native_av] DirectYuv output hw frame allocation failed: {}", error);
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }

    auto* frames = direct.decoded->hw_frames_ctx
        ? reinterpret_cast<AVHWFramesContext*>(direct.decoded->hw_frames_ctx->data)
        : nullptr;
    if (!frames || (frames->sw_format != AV_PIX_FMT_NV12 &&
                    frames->sw_format != AV_PIX_FMT_P010)) {
        spdlog::error("[native_av] DirectCudaYuv requires NV12 or P010 input");
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    const bool p010 = frames->sw_format == AV_PIX_FMT_P010;
    if (p010 && (gpu_frame->linesize[0] < options_.width * 2 ||
                 gpu_frame->linesize[1] < options_.width * 2)) {
        spdlog::error(
            "[native_av] DirectCudaYuv P010 rejected: CUDA hwframe stride "
            "does not expose the required 16-bit layout (y_stride={} uv_stride={} "
            "required={})",
            gpu_frame->linesize[0], gpu_frame->linesize[1], options_.width * 2);
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    const auto launch_t0 = Clock::now();
    bool launch_ok = false;
    if (has_overlay) {
        launch_ok = direct_yuv_compositor_->composite_direct_nv12_batch(
            direct.program->batch, direct.program->resources,
            reinterpret_cast<CUdeviceptr>(direct.decoded->data[0]),
            direct.decoded->linesize[0],
            reinterpret_cast<CUdeviceptr>(direct.decoded->data[1]),
            direct.decoded->linesize[1],
            reinterpret_cast<CUdeviceptr>(gpu_frame->data[0]),
            gpu_frame->linesize[0],
            reinterpret_cast<CUdeviceptr>(gpu_frame->data[1]),
            gpu_frame->linesize[1],
            static_cast<std::uint32_t>(options_.width),
            static_cast<std::uint32_t>(options_.height), cuda_stream_,
            p010 ? media::video::CudaYuvFormat::P010
                 : media::video::CudaYuvFormat::Nv12);
    } else {
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = reinterpret_cast<CUdeviceptr>(direct.decoded->data[0]);
        copy.srcPitch = direct.decoded->linesize[0];
        copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.dstDevice = reinterpret_cast<CUdeviceptr>(gpu_frame->data[0]);
        copy.dstPitch = gpu_frame->linesize[0];
        copy.WidthInBytes = static_cast<std::size_t>(options_.width) * (p010 ? 2u : 1u);
        copy.Height = static_cast<std::size_t>(options_.height);
        CUresult copy_result = cuMemcpy2DAsync(&copy, cuda_stream_);
        launch_ok = copy_result == CUDA_SUCCESS;
        if (!launch_ok) {
            const char* name = nullptr;
            const char* detail = nullptr;
            cuGetErrorName(copy_result, &name);
            cuGetErrorString(copy_result, &detail);
            spdlog::error(
                "[native_av] DirectYuv luma copy failed: {} ({}) format={} "
                "src_pitch={} dst_pitch={} width_bytes={} height={} src={} dst={}",
                name ? name : "CUDA error", detail ? detail : "unknown",
                p010 ? "p010" : "nv12", direct.decoded->linesize[0],
                gpu_frame->linesize[0], copy.WidthInBytes, copy.Height,
                reinterpret_cast<void*>(copy.srcDevice),
                reinterpret_cast<void*>(copy.dstDevice));
        }
        if (launch_ok) {
            copy.srcDevice = reinterpret_cast<CUdeviceptr>(direct.decoded->data[1]);
            copy.srcPitch = direct.decoded->linesize[1];
            copy.dstDevice = reinterpret_cast<CUdeviceptr>(gpu_frame->data[1]);
            copy.dstPitch = gpu_frame->linesize[1];
            copy.Height = static_cast<std::size_t>(options_.height / 2);
            copy_result = cuMemcpy2DAsync(&copy, cuda_stream_);
            launch_ok = copy_result == CUDA_SUCCESS;
            if (!launch_ok) {
                const char* name = nullptr;
                const char* detail = nullptr;
                cuGetErrorName(copy_result, &name);
                cuGetErrorString(copy_result, &detail);
                spdlog::error(
                    "[native_av] DirectYuv chroma copy failed: {} ({}) format={} "
                    "src_pitch={} dst_pitch={} width_bytes={} height={} src={} dst={}",
                    name ? name : "CUDA error", detail ? detail : "unknown",
                    p010 ? "p010" : "nv12", direct.decoded->linesize[1],
                    gpu_frame->linesize[1], copy.WidthInBytes, copy.Height,
                    reinterpret_cast<void*>(copy.srcDevice),
                    reinterpret_cast<void*>(copy.dstDevice));
            }
        }
    }
    direct_yuv_cuda_launch_ms_ += elapsed_ms(launch_t0);
    if (!launch_ok) {
        spdlog::error("[native_av] DirectCudaYuv kernel dispatch failed");
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    gpu_frame->color_range = codec_->color_range;
    gpu_frame->colorspace = codec_->colorspace;
    gpu_frame->color_trc = codec_->color_trc;
    gpu_frame->color_primaries = codec_->color_primaries;
    gpu_frame->pts = static_cast<int64_t>(frames_submitted_++);
    cuda::OwnedCudaEvent ready{};
    const CUresult event_create = cuda::create_recorded(
        ready, reinterpret_cast<CUcontext>(cuda_context_), cuda_stream_);
    const CUresult event_record = event_create;
    if (event_create != CUDA_SUCCESS || event_record != CUDA_SUCCESS) {
        const char* name = nullptr;
        const char* detail = nullptr;
        cuGetErrorName(event_record, &name);
        cuGetErrorString(event_record, &detail);
        spdlog::error("[native_av] DirectYuv CUDA event setup failed: {} ({})",
                      name ? name : "CUDA error", detail ? detail : "unknown");
        cuda::destroy(ready);
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    // Do not wait for the producer event here.  Direct-YUV is a bounded
    // producer/consumer pipeline: the CUDA composite/copy is enqueued on the
    // producer stream and the single encoder owner consumes completed frames
    // from pending_cuda_frames_ in drain_ready_cuda_frames().  Waiting here
    // collapses the existing ring to depth one and makes NVDEC/composite and
    // NVENC run serially.  The consumer still waits when the bounded ring is
    // full, preserving ordering, frame lifetime and fail-closed CUDA errors.
    pending_cuda_frames_.push_back(
        PendingCudaFrame{gpu_frame, ready,
                         runtime::kInvalidRenderSurfaceHandle,
                         direct.program,
                         chronon3d::media::HwFrameRef(direct.decoded)});
    cuda_pending_peak_ = std::max<std::uint64_t>(
        cuda_pending_peak_, pending_cuda_frames_.size());
    if (counters_) {
        auto observed = counters_->cuda_encode_queue_peak.load(std::memory_order_relaxed);
        const auto current = static_cast<std::uint64_t>(pending_cuda_frames_.size());
        while (observed < current &&
               !counters_->cuda_encode_queue_peak.compare_exchange_weak(
                   observed, current, std::memory_order_relaxed)) {}
    }
    const bool drained = drain_ready_cuda_frames(false);
    last_frame_telemetry_.encoder_ms = elapsed_ms(direct_submit_t0);
    last_frame_telemetry_.frame_submit_ms = last_frame_telemetry_.encoder_ms;
    return drained;
#endif
}

} // namespace chronon3d::cli
