#include "native_av_encoder.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <mutex>

#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#include <cuda.h>
#endif

namespace chronon3d::cli {

bool NativeAvEncoder::prepare_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle destination) {
#if !defined(CHRONON3D_ENABLE_CUDA_INTEROP) || !defined(CHRONON3D_ENABLE_VULKAN)
    (void)backend; (void)destination;
    return false;
#else
    if (options_.direct_yuv_mode) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!gpu_nvenc_ || !cuda_context_ || !cuda_frames_ref_ ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_)) != CUDA_SUCCESS) {
        spdlog::error("[native_av] failed to activate CUDA context before surface prepare");
        return false;
    }
    auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend);
    if (!vulkan) return false;
    if (!vulkan->cuda_context_matches_device(reinterpret_cast<CUcontext>(cuda_context_))) {
        spdlog::error("[native_av] Vulkan/CUDA physical-device UUID mismatch");
        return false;
    }
    const auto key = static_cast<std::uint64_t>(destination);
    auto compositor_it = cuda_nv12_compositors_.find(key);
    static const bool s_trace = std::getenv("CHRONON_CUDA_INTEROP_TRACE") != nullptr;
    if (compositor_it == cuda_nv12_compositors_.end()) {
        const auto info = vulkan->export_cuda_external_memory(destination);
        auto compositor = std::make_unique<media::video::CudaNv12Compositor>(
            info, reinterpret_cast<CUcontext>(cuda_context_));
        cuda_nv12_compositors_.emplace(key, std::move(compositor));
        if (s_trace) {
            spdlog::info("[interop-trace] PREPARE_NATIVE_SURFACE dest={} ctx={}", destination, cuda_context_);
        }
    }
    // Materialize FFmpeg's CUDA frame pool before the render loop. The frame
    // objects are retained and reused by write_native_surface_impl; this
    // moves the first-use AVFrame/hwframe allocations into the explicit
    // warm-up phase instead of allowing them to appear as hot-loop stalls.
    while (reusable_cuda_frames_.size() + pending_cuda_frames_.size() <
           kCudaEncodeRingSlots) {
        AVFrame* frame = av_frame_alloc();
        if (!frame) return false;
        frame->format = AV_PIX_FMT_CUDA;
        frame->width = options_.width;
        frame->height = options_.height;
        frame->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
        if (!frame->hw_frames_ctx ||
            av_hwframe_get_buffer(cuda_frames_ref_, frame, 0) < 0) {
            av_frame_free(&frame);
            return false;
        }
        av_frame_unref(frame);
        reusable_cuda_frames_.push_back(frame);
    }
    return true;
#endif
}

bool NativeAvEncoder::write_native_surface_impl(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination,
    bool surface_already_prepared) {
#if !defined(CHRONON3D_ENABLE_CUDA_INTEROP) || !defined(CHRONON3D_ENABLE_VULKAN)
    (void)backend; (void)source; (void)destination; (void)surface_already_prepared;
    return false;
#else
    // Z1 typed-feed guard: GPU surface feeds belong to the FullGraph native
    // session only. A native surface arriving on a DirectYUV session has no
    // graph output behind it (the DirectYuv executor owns the frame loop).
    if (options_.direct_yuv_mode) {
        spdlog::error(
            "[native_av] GPU surface feed refused on a DirectYUV session "
            "(typed encoder capability mismatch)");
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!gpu_nvenc_ || !cuda_context_ || !cuda_frames_ref_ ||
        source == runtime::kInvalidRenderSurfaceHandle ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        spdlog::error("[native_av] invalid GPU frame contract source={} destination={} gpu_nvenc={} cuda_context={} frames_ctx={}",
                      source, destination, gpu_nvenc_, cuda_context_ != nullptr,
                      cuda_frames_ref_ != nullptr);
        return false;
    }
    if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_)) != CUDA_SUCCESS) {
        spdlog::error("[native_av] failed to activate CUDA context before surface handoff");
        return false;
    }
    static const bool s_trace = std::getenv("CHRONON_CUDA_INTEROP_TRACE") != nullptr;
    if (s_trace) {
        spdlog::info("[interop-trace] WRITE_NATIVE_SURFACE pts={} src={} dest={} prepared={} ctx={} stream={}",
                     frames_submitted_, source, destination, surface_already_prepared,
                     cuda_context_, static_cast<void*>(cuda_stream_));
    }
    auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend);
    if (!vulkan) return false;
    if (!vulkan->cuda_context_matches_device(reinterpret_cast<CUcontext>(cuda_context_))) {
        spdlog::error("[native_av] Vulkan/CUDA physical-device UUID mismatch");
        return false;
    }
    const auto key = static_cast<std::uint64_t>(destination);
    auto compositor_it = cuda_nv12_compositors_.find(key);
    if (compositor_it == cuda_nv12_compositors_.end()) {
        const auto info = vulkan->export_cuda_external_memory(destination);
        auto compositor = std::make_unique<media::video::CudaNv12Compositor>(
            info, reinterpret_cast<CUcontext>(cuda_context_));
        compositor_it = cuda_nv12_compositors_.emplace(key, std::move(compositor)).first;
    }

    if (!surface_already_prepared && source != destination) {
        // FullGraph native output must be rendered into the encode surface.
        // A Vulkan→CUDA/Vulkan D2D copy here hides a bandwidth-heavy fallback
        // behind the encoder API and invalidates the zero-copy contract.
        spdlog::error(
            "[native_av] unprepared native source {} is not the encode surface {}; "
            "refusing the legacy D2D copy lane",
            source, destination);
        return false;
    }
    try {
        if (!drain_ready_cuda_frames(false)) return false;
        if (pending_cuda_frames_.size() >= kCudaEncodeRingSlots &&
            !drain_ready_cuda_frames(true)) {
            spdlog::error("[native_av] CUDA encoder queue backpressure failed");
            return false;
        }

        AVFrame* gpu_frame = nullptr;
        if (!reusable_cuda_frames_.empty()) {
            gpu_frame = reusable_cuda_frames_.front();
            reusable_cuda_frames_.pop_front();
            av_frame_unref(gpu_frame);
        } else {
            gpu_frame = av_frame_alloc();
        }
        if (!gpu_frame) return false;
        gpu_frame->format = AV_PIX_FMT_CUDA;
        gpu_frame->width = options_.width;
        gpu_frame->height = options_.height;
        gpu_frame->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
        if (av_hwframe_get_buffer(cuda_frames_ref_, gpu_frame, 0) < 0) {
            spdlog::error("[native_av] av_hwframe_get_buffer failed for GPU frame {}", frames_written_);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        if (!gpu_frame->data[0] || !gpu_frame->data[1]) {
            spdlog::error("[native_av] NV12 CUDA frame has no Y/UV planes");
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        const auto conversion_t0 = std::chrono::steady_clock::now();
        if (!compositor_it->second->composite_surface_to_nv12(
                reinterpret_cast<CUdeviceptr>(gpu_frame->data[0]),
                gpu_frame->linesize[0],
                reinterpret_cast<CUdeviceptr>(gpu_frame->data[1]),
                gpu_frame->linesize[1],
                static_cast<std::uint32_t>(options_.width),
                static_cast<std::uint32_t>(options_.height), cuda_stream_)) {
            spdlog::error("[native_av] direct RGBA->NV12 CUDA compositor failed");
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        const double conversion_ms = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - conversion_t0).count()) /
            1000.0;
        native_convert_ms_ += conversion_ms;
        // The CUDA conversion has signaled C2V. Register that handoff so
        // the next Vulkan frame waits before rewriting this encode surface.
        const auto cuda_ready = vulkan->prepare_cuda_surface_for_vulkan(destination);
        if (!cuda_ready.ok()) {
            spdlog::error("[native_av] failed to register CUDA-to-Vulkan surface handoff: {}",
                          cuda_ready.error().message);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        if (counters_) {
            counters_->native_surface_yuv_frames.fetch_add(1, std::memory_order_relaxed);
            counters_->native_surface_yuv_bytes.fetch_add(
                static_cast<std::uint64_t>(options_.width) *
                    static_cast<std::uint64_t>(options_.height) * 3 / 2,
                std::memory_order_relaxed);
        }
        gpu_frame->color_range = codec_->color_range;
        gpu_frame->colorspace = codec_->colorspace;
        gpu_frame->color_trc = codec_->color_trc;
        gpu_frame->color_primaries = codec_->color_primaries;
        gpu_frame->pts = static_cast<int64_t>(frames_submitted_++);
        cuda::OwnedCudaEvent ready{};
        const CUresult event_create = cuda::create_recorded(
            ready, reinterpret_cast<CUcontext>(cuda_context_), cuda_stream_);
        if (event_create != CUDA_SUCCESS) {
            const char* name = nullptr;
            const char* detail = nullptr;
            cuGetErrorName(event_create, &name);
            cuGetErrorString(event_create, &detail);
            spdlog::error("[native_av] CUDA encode event setup failed: {} ({})",
                          name ? name : "CUDA error", detail ? detail : "unknown");
            cuda::destroy(ready);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        pending_cuda_frames_.push_back(
            PendingCudaFrame{gpu_frame, ready, destination, nullptr, {}});
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
        // The current write call owns the conversion interval above even
        // when NVENC submission is deferred in pending_cuda_frames_. Keep the
        // per-frame encoder record aligned with the package being written;
        // asynchronous packet timing remains reported by the writer interval.
        last_frame_telemetry_ = EncoderFrameTelemetry{};
        last_frame_telemetry_.conversion_copy_ms = conversion_ms;
        last_frame_telemetry_.native_convert_ms = conversion_ms;
        last_frame_telemetry_.frame_submit_ms = conversion_ms;
        return drained;
    } catch (const std::exception& error) {
        spdlog::error("[native_av] CUDA/Vulkan frame handoff failed: {}", error.what());
        return false;
    }
#endif
}

bool NativeAvEncoder::finish_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle destination) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)backend; (void)destination;
    return true;
#else
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    (void)backend;
    for (;;) {
        auto it = std::find_if(
            pending_cuda_frames_.begin(), pending_cuda_frames_.end(),
            [destination](const PendingCudaFrame& pending) {
                return pending.surface == destination;
            });
        if (it == pending_cuda_frames_.end()) return true;
        if (!drain_ready_cuda_frames(true)) return false;
    }
#endif
}

bool NativeAvEncoder::poll_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle destination) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)backend; (void)destination;
    return true;
#else
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    (void)backend;
    if (!drain_ready_cuda_frames(false)) return false;
    const auto it = std::find_if(
        pending_cuda_frames_.begin(), pending_cuda_frames_.end(),
        [destination](const PendingCudaFrame& pending) {
            return pending.surface == destination;
        });
    if (it == pending_cuda_frames_.end()) return true;
    return cuda::query(it->ready) == CUDA_SUCCESS;
#endif
}

} // namespace chronon3d::cli
