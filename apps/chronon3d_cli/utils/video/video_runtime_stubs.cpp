#include <chronon3d/media/video/video_device_runtime.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d::media {

std::shared_ptr<VideoDeviceRuntime> VideoDeviceRuntime::create(
    runtime::DeviceId device, std::shared_ptr<runtime::GpuRuntime> gpu,
    std::string& reason, std::int32_t cuda_device_ordinal) {
    reason.clear();
    return std::shared_ptr<VideoDeviceRuntime>(new VideoDeviceRuntime(
        device, gpu ? std::move(gpu) : std::make_shared<runtime::GpuRuntime>(),
        cuda_device_ordinal < 0 ? static_cast<std::int32_t>(device)
                                : cuda_device_ordinal));
}

VideoDeviceRuntime::VideoDeviceRuntime(
    runtime::DeviceId device, std::shared_ptr<runtime::GpuRuntime> gpu,
    std::int32_t ordinal)
    : device_(device), cuda_device_ordinal_(ordinal), gpu_(std::move(gpu)) {}

VideoDeviceRuntime::~VideoDeviceRuntime() = default;

AVBufferRef* VideoDeviceRuntime::ref_cuda_hwdevice() {
    return nullptr;
}

bool VideoDeviceRuntime::acquire_slot_surfaces(
    runtime::FrameExecutionSlot&,
    runtime::RenderSurfaceRegistry&,
    graph::RenderBackend&,
    std::uint32_t,
    std::uint32_t,
    std::string& reason) {
    reason = "native video surfaces unavailable in this build";
    return false;
}

bool VideoDeviceRuntime::context_matches(std::uintptr_t) const noexcept {
    return false;
}

std::shared_ptr<VideoDeviceRuntime> VideoRuntimeRegistry::get_or_create(
    runtime::DeviceId device, std::shared_ptr<runtime::GpuRuntime> gpu,
    std::int32_t ordinal) {
    std::lock_guard lock(mutex_);
    if (const auto it = runtimes_.find(device); it != runtimes_.end()) {
        if (ordinal >= 0 && cuda_ordinals_.contains(device) &&
            cuda_ordinals_.at(device) != ordinal) {
            spdlog::error("[video-runtime] software build rejected CUDA ordinal mismatch");
            return nullptr;
        }
        return it->second;
    }
    std::string reason;
    auto runtime = VideoDeviceRuntime::create(device, std::move(gpu), reason, ordinal);
    // The software build has no FFmpeg/CUDA device and must never advertise
    // one, but it still provides the same process-lifetime registry identity.
    runtimes_.emplace(device, runtime);
    cuda_ordinals_[device] = ordinal;
    return runtime;
}

std::size_t VideoRuntimeRegistry::size() const noexcept {
    std::lock_guard lock(mutex_);
    return runtimes_.size();
}

} // namespace chronon3d::media
