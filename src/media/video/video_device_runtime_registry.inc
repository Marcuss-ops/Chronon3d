namespace chronon3d::media {

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
        if (profiling::g_current_counters) {
            profiling::g_current_counters->video_runtime_reused
                .fetch_add(1, std::memory_order_relaxed);
        }
        spdlog::debug("[video-runtime] reused persistent runtime for device {}", device);
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
    if (profiling::g_current_counters) {
        profiling::g_current_counters->video_runtime_created
            .fetch_add(1, std::memory_order_relaxed);
    }
    spdlog::info("[video-runtime] created persistent runtime for device {} "
                 "(CUDA ordinal {})", device, cuda_ordinals_[device]);
    return runtime;
}

std::size_t VideoRuntimeRegistry::size() const noexcept {
    std::lock_guard lock(mutex_);
    return runtimes_.size();
}

} // namespace chronon3d::media
