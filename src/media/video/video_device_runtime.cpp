#include <chronon3d/media/video/video_device_runtime.hpp>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
}

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <string>
#include <utility>

namespace chronon3d::media {

std::shared_ptr<VideoDeviceRuntime> VideoDeviceRuntime::create(
    runtime::DeviceId device,
    std::shared_ptr<runtime::GpuRuntime> gpu,
    std::string& reason) {
    reason.clear();
    if (!gpu) gpu = std::make_shared<runtime::GpuRuntime>();
    return std::shared_ptr<VideoDeviceRuntime>(new VideoDeviceRuntime(device, std::move(gpu)));
}

VideoDeviceRuntime::VideoDeviceRuntime(
    runtime::DeviceId device,
    std::shared_ptr<runtime::GpuRuntime> gpu)
    : device_(device), gpu_(std::move(gpu)) {}

VideoDeviceRuntime::~VideoDeviceRuntime() {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (cuda_hwdevice_) {
        av_buffer_unref(&cuda_hwdevice_);
    }
#endif
}

bool VideoDeviceRuntime::ensure_initialized(std::string& reason) {
    std::lock_guard lock(mutex_);
    if (initialized_) return init_ok_;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (!gpu_->initialize(device_)) {
        reason = "GpuRuntime failed to initialize CUDA for device " + std::to_string(device_);
        initialized_ = true;
        init_ok_ = false;
        return false;
    }

    // FFmpeg must use the SAME primary CUDA context retained by GpuRuntime
    // (AV_CUDA_USE_PRIMARY_CONTEXT); otherwise device pointers and streams
    // created by one context are invalid in the other.
    const std::string device_string = std::to_string(device_);
    AVBufferRef* hwdev = nullptr;
    if (av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA,
                               device_string.c_str(), nullptr,
                               AV_CUDA_USE_PRIMARY_CONTEXT) < 0) {
        reason = "av_hwdevice_ctx_create(CUDA, primary context) failed for device " + device_string;
        initialized_ = true;
        init_ok_ = false;
        return false;
    }

    auto* av_device = reinterpret_cast<AVHWDeviceContext*>(hwdev->data);
    auto* av_cuda = av_device
        ? reinterpret_cast<AVCUDADeviceContext*>(av_device->hwctx)
        : nullptr;
    // FAIL_CLOSED.  One device: 1 CUDA primary context, 1 FFmpeg hwdevice
    // aliasing it, N encoders/decoders/CUDA stages borrowing refs.
    if (!av_cuda || !av_cuda->cuda_ctx ||
        !context_matches(reinterpret_cast<std::uintptr_t>(av_cuda->cuda_ctx))) {
        reason = "FAIL_CLOSED: FFmpeg CUDA hwdevice context does not match the "
                 "GpuRuntime primary context";
        av_buffer_unref(&hwdev);
        initialized_ = true;
        init_ok_ = false;
        return false;
    }

    cuda_hwdevice_ = hwdev;
#endif
    initialized_ = true;
    init_ok_ = true;
    return true;
}

bool VideoDeviceRuntime::context_matches(std::uintptr_t context) const noexcept {
    if (context == 0 || !gpu_ || !gpu_->is_initialized()) return false;
    return context == gpu_->native_context_handle();
}

AVBufferRef* VideoDeviceRuntime::ref_cuda_hwdevice() {
    std::string reason;
    if (!ensure_initialized(reason)) {
        spdlog::error("[video-runtime] device {} CUDA hwdevice unavailable: {}",
                      device_, reason);
        return nullptr;
    }
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    return cuda_hwdevice_ ? av_buffer_ref(cuda_hwdevice_) : nullptr;
#else
    return nullptr;
#endif
}

std::shared_ptr<VideoDeviceRuntime> VideoRuntimeRegistry::get_or_create(
    runtime::DeviceId device,
    std::shared_ptr<runtime::GpuRuntime> gpu) {
    std::lock_guard lock(mutex_);
    auto it = runtimes_.find(device);
    if (it != runtimes_.end()) return it->second;

    // Creation and insertion happen under one lock. This prevents two
    // concurrent jobs from constructing two GPU owners for the same device.
    if (!gpu) {
        auto gpu_it = gpu_runtimes_.find(device);
        if (gpu_it != gpu_runtimes_.end()) {
            gpu = gpu_it->second;
        } else {
            gpu = std::make_shared<runtime::GpuRuntime>();
            gpu_runtimes_.emplace(device, gpu);
        }
    } else {
        auto [gpu_it, inserted] = gpu_runtimes_.emplace(device, gpu);
        if (!inserted) gpu = gpu_it->second;
    }

    std::string reason;
    auto runtime = VideoDeviceRuntime::create(device, std::move(gpu), reason);
    if (!runtime) {
        spdlog::error("[video-runtime] failed to create device runtime {}: {}",
                      device, reason);
        return nullptr;
    }
    // The lock is still held from the lookup/creation above, so this insert
    // cannot race with another owner for the same device.
    runtimes_.emplace(device, runtime);
    return runtime;
}

std::size_t VideoRuntimeRegistry::size() const noexcept {
    std::lock_guard lock(mutex_);
    return runtimes_.size();
}

} // namespace chronon3d::media