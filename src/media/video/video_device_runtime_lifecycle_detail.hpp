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
    : device_(device), cuda_device_ordinal_(cuda_device_ordinal), gpu_(std::move(gpu)) {
    image_cache_.set_backend(std::make_shared<image::StbImageBackend>());
}

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
    if (initialized_) {
        if (init_ok_ && profiling::g_current_counters) {
            profiling::g_current_counters->cuda_hwdevice_reused
                .fetch_add(1, std::memory_order_relaxed);
        }
        return init_ok_;
    }

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
    if (profiling::g_current_counters) {
        profiling::g_current_counters->cuda_hwdevice_created
            .fetch_add(1, std::memory_order_relaxed);
    }
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

} // namespace chronon3d::media
