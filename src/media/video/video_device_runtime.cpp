#include <chronon3d/media/video/video_device_runtime.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <chronon3d/media/video/cuda_image_resource.hpp>
#endif

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <libavutil/hwcontext_cuda.h>
#endif
}

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <string>
#include <utility>
#include <chrono>

namespace chronon3d::media {

std::shared_ptr<VideoDeviceRuntime> VideoDeviceRuntime::create(
    runtime::DeviceId device,
    std::shared_ptr<runtime::GpuRuntime> gpu,
    std::string& reason,
    std::int32_t cuda_device_ordinal) {
    reason.clear();
    if (!gpu) gpu = std::make_shared<runtime::GpuRuntime>();
    return std::shared_ptr<VideoDeviceRuntime>(new VideoDeviceRuntime(
        device, std::move(gpu), cuda_device_ordinal < 0
            ? static_cast<std::int32_t>(device) : cuda_device_ordinal));
}

VideoDeviceRuntime::VideoDeviceRuntime(
    runtime::DeviceId device,
    std::shared_ptr<runtime::GpuRuntime> gpu,
    std::int32_t cuda_device_ordinal)
    : device_(device), cuda_device_ordinal_(cuda_device_ordinal), gpu_(std::move(gpu)) {}

VideoDeviceRuntime::~VideoDeviceRuntime() {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (auto& [key, frames] : cuda_frames_) {
        (void)key;
        if (frames) av_buffer_unref(&frames);
    }
    cuda_frames_.clear();
    if (cuda_hwdevice_) {
        av_buffer_unref(&cuda_hwdevice_);
    }
#endif
}

bool VideoDeviceRuntime::ensure_initialized(std::string& reason) {
    std::lock_guard lock(mutex_);
    if (initialized_) return init_ok_;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (!gpu_->initialize(static_cast<std::uint32_t>(cuda_device_ordinal_))) {
        reason = "GpuRuntime failed to initialize CUDA ordinal " +
                 std::to_string(cuda_device_ordinal_);
        initialized_ = true;
        init_ok_ = false;
        return false;
    }

    // FFmpeg must use the SAME primary CUDA context retained by GpuRuntime
    // (AV_CUDA_USE_PRIMARY_CONTEXT); otherwise device pointers and streams
    // created by one context are invalid in the other.
    // `device_` is the daemon's logical scheduler id, not a CUDA ordinal.
    // FFmpeg's CUDA device argument is ordinal-based; using the logical id
    // silently binds the wrong GPU on a multi-device daemon.  The registry
    // resolves and validates the ordinal before initialization.
    const std::int32_t ffmpeg_cuda_ordinal = cuda_device_ordinal_ >= 0
        ? cuda_device_ordinal_ : static_cast<std::int32_t>(device_);
    const std::string device_string = std::to_string(ffmpeg_cuda_ordinal);
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

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
AVBufferRef* VideoDeviceRuntime::ref_cuda_frames(
    std::uint32_t width, std::uint32_t height, int sw_format,
    std::string& reason) {
    if (width == 0 || height == 0) {
        reason = "invalid CUDA frames dimensions";
        return nullptr;
    }
    if (!ensure_initialized(reason)) return nullptr;
    std::lock_guard lock(mutex_);
    const CudaFramesKey key{width, height, sw_format};
    if (const auto it = cuda_frames_.find(key); it != cuda_frames_.end()) {
        return av_buffer_ref(it->second);
    }
    if (!cuda_hwdevice_) {
        reason = "CUDA hwdevice is unavailable for frames context";
        return nullptr;
    }
    AVBufferRef* frames_ref = av_hwframe_ctx_alloc(cuda_hwdevice_);
    if (!frames_ref) {
        reason = "av_hwframe_ctx_alloc failed";
        return nullptr;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
    frames->format = AV_PIX_FMT_CUDA;
    frames->sw_format = static_cast<AVPixelFormat>(sw_format);
    frames->width = static_cast<int>(width);
    frames->height = static_cast<int>(height);
    frames->initial_pool_size = 8;
    if (av_hwframe_ctx_init(frames_ref) < 0) {
        reason = "av_hwframe_ctx_init failed";
        av_buffer_unref(&frames_ref);
        return nullptr;
    }
    cuda_frames_.emplace(key, frames_ref);
    return av_buffer_ref(frames_ref);
}
#endif

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
std::shared_ptr<const CudaImageResource> VideoDeviceRuntime::get_or_upload_image(
    const assets::ContentDigest& digest,
    const ImageDecodeOptions& options,
    std::uint32_t width,
    std::uint32_t height,
    std::span<const float> rgba,
    bool& cache_hit,
    double& upload_ms,
    std::string& reason) {
    cache_hit = false;
    upload_ms = 0.0;
    if (width == 0 || height == 0 || rgba.empty()) {
        reason = "invalid CUDA image dimensions or payload";
        return nullptr;
    }
    if (digest == assets::ContentDigest{}) {
        reason = "CUDA image cache requires a valid content digest";
        return nullptr;
    }
    if (!ensure_initialized(reason)) return nullptr;
    if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(gpu_->native_context_handle())) != CUDA_SUCCESS) {
        reason = "failed to select owning CUDA context for image cache";
        return nullptr;
    }

    const CudaImageKey key{digest, options, width, height};
    std::lock_guard lock(mutex_);
    if (const auto it = cuda_images_.find(key); it != cuda_images_.end()) {
        cache_hit = true;
        return it->second;
    }

    auto resource = std::make_shared<CudaImageResource>();
    resource->owner_gpu = gpu_;
    const std::size_t expected = static_cast<std::size_t>(width) * height * 4 * sizeof(float);
    if (rgba.size_bytes() != expected) {
        reason = "CUDA image payload size does not match dimensions";
        return nullptr;
    }
    const auto started = std::chrono::steady_clock::now();
    if (cuMemAlloc(&resource->ptr, expected) != CUDA_SUCCESS ||
        cuMemcpyHtoD(resource->ptr, rgba.data(), expected) != CUDA_SUCCESS) {
        reason = "failed to upload image into CUDA resident cache";
        return nullptr;
    }
    const auto finished = std::chrono::steady_clock::now();
    upload_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    resource->width = width;
    resource->height = height;
    resource->pitch_bytes = static_cast<std::size_t>(width) * 4 * sizeof(float);
    std::shared_ptr<const CudaImageResource> published = std::move(resource);
    cuda_images_.emplace(key, published);
    return published;
}
#endif

std::shared_ptr<VideoDeviceRuntime> VideoRuntimeRegistry::get_or_create(
    runtime::DeviceId device,
    std::shared_ptr<runtime::GpuRuntime> gpu,
    std::int32_t cuda_device_ordinal) {
    std::lock_guard lock(mutex_);
    auto it = runtimes_.find(device);
    if (it != runtimes_.end()) {
        const auto ordinal_it = cuda_ordinals_.find(device);
        if (cuda_device_ordinal >= 0 && ordinal_it != cuda_ordinals_.end() &&
            ordinal_it->second != cuda_device_ordinal) {
            spdlog::error(
                "[video-runtime] FAIL_CLOSED: device {} was already bound to CUDA ordinal {}, "
                "new job requested {}",
                device, ordinal_it->second, cuda_device_ordinal);
            return nullptr;
        }
        return it->second;
    }

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
    const auto ordinal_it = cuda_ordinals_.find(device);
    const auto ordinal = ordinal_it == cuda_ordinals_.end()
        ? cuda_device_ordinal : ordinal_it->second;
    auto runtime = VideoDeviceRuntime::create(device, std::move(gpu), reason, ordinal);
    if (!runtime) {
        spdlog::error("[video-runtime] failed to create device runtime {}: {}",
                      device, reason);
        return nullptr;
    }
    // The lock is still held from the lookup/creation above, so this insert
    // cannot race with another owner for the same device.
    runtimes_.emplace(device, runtime);
    cuda_ordinals_[device] = ordinal < 0
        ? static_cast<std::int32_t>(device) : ordinal;
    return runtime;
}

std::size_t VideoRuntimeRegistry::size() const noexcept {
    std::lock_guard lock(mutex_);
    return runtimes_.size();
}

} // namespace chronon3d::media
