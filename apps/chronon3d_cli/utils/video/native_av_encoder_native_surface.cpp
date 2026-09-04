#include "native_av_encoder.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <exception>
#include <mutex>

#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#include <cuda.h>
#endif

namespace chronon3d::cli {

bool NativeAvEncoder::write_native_surface_impl(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination,
    bool surface_already_prepared) {
#if !defined(CHRONON3D_ENABLE_CUDA_INTEROP) || !defined(CHRONON3D_ENABLE_VULKAN)
    (void)backend; (void)source; (void)destination; (void)surface_already_prepared;
    return false;
#else
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
    auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend);
    if (!vulkan) return false;
    if (!vulkan->cuda_context_matches_device(reinterpret_cast<CUcontext>(cuda_context_))) {
        spdlog::error("[native_av] Vulkan/CUDA physical-device UUID mismatch");
        return false;
    }
    if (!surface_already_prepared) {
        const auto copy_result = vulkan->copy_surface_to_cuda_encoder(source, destination, false);
        if (!copy_result.ok()) {
            spdlog::error("[native_av] GPU surface conversion failed: {}", copy_result.error().message);
            return false;
        }
    }
    try {
        if (!drain_ready_cuda_frames(false)) return false;
        if (pending_cuda_frames_.size() >= kCudaEncodeRingSlots &&
            !drain_ready_cuda_frames(true)) {
            spdlog::error("[native_av] CUDA encoder queue backpressure failed");
            return false;
        }

        const auto key = static_cast<std::uint64_t>(destination);
        auto compositor_it = cuda_nv12_compositors_.find(key);
        if (compositor_it == cuda_nv12_compositors_.end()) {
            const auto info = vulkan->export_cuda_external_memory(destination);
            auto compositor = std::make_unique<backends::vulkan::CudaNv12SurfaceCompositor>(
                info, reinterpret_cast<CUcontext>(cuda_context_));
            compositor_it = cuda_nv12_compositors_.emplace(key, std::move(compositor)).first;
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
        if (counters_) {
            counters_->native_surface_yuv_frames.fetch_add(1, std::memory_order_relaxed);
            counters_->native_surface_yuv_bytes.fetch_add(
                static_cast<std::uint64_t>(options_.width) *
                    static_cast<std::uint64_t>(options_.height) * 3 / 2,
                std::memory_order_relaxed);
        }
        const auto prepared = vulkan->prepare_cuda_surface_for_vulkan(destination);
        if (!prepared.ok()) {
            spdlog::error("[native_av] failed to publish CUDA completion for surface {}: {}",
                          destination, prepared.error().message);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
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
        spdlog::info("[native_av_diag] push frame_pts={} event={} owner_ctx={} dest={}",
                     gpu_frame->pts, static_cast<void*>(ready.event),
                     static_cast<void*>(ready.owner_context), destination);
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
        return drain_ready_cuda_frames(false);
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
    spdlog::info("[native_av] finish_native_surface destination={} pending={}",
                 destination, pending_cuda_frames_.size());
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
