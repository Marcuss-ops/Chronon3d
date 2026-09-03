#pragma once

#include <atomic>
#include <stdexcept>

namespace chronon3d::runtime {

/// Fatal GPU failure boundary. A backend throws this only after the device has
/// reported a terminal device-loss condition; callers must not retry work on
/// the same worker/device instance.
class GpuDeviceLostError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Worker-wide poison bit. Device loss is terminal for the current process:
/// the daemon/orchestrator must replace the worker instead of rebuilding the
/// VkDevice or silently falling back inside the same job.
inline std::atomic<bool>& gpu_worker_poisoned_flag() noexcept {
    static std::atomic<bool> poisoned{false};
    return poisoned;
}

inline void poison_gpu_worker() noexcept {
    gpu_worker_poisoned_flag().store(true, std::memory_order_release);
}

[[nodiscard]] inline bool gpu_worker_poisoned() noexcept {
    return gpu_worker_poisoned_flag().load(std::memory_order_acquire);
}

inline void require_healthy_gpu_worker() {
    if (gpu_worker_poisoned()) {
        throw GpuDeviceLostError{
            "GPU worker is poisoned after a terminal device-loss failure"};
    }
}

} // namespace chronon3d::runtime
