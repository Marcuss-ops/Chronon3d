#pragma once

#include <array>
#include <cstddef>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using VulkanExternalNativeHandle = HANDLE;
inline constexpr VulkanExternalNativeHandle kInvalidVulkanExternalNativeHandle = nullptr;
#else
#include <unistd.h>
using VulkanExternalNativeHandle = int;
inline constexpr VulkanExternalNativeHandle kInvalidVulkanExternalNativeHandle = -1;
#endif
#include <utility>

namespace chronon3d::backends::vulkan {

// Scoped ownership for the external-memory + two-semaphore handoff. POSIX
// descriptors transfer to CUDA on import; Windows NT handles remain owned by
// the application and are closed after import or when construction fails.
class VulkanExternalFds {
public:
    explicit VulkanExternalFds(
        VulkanExternalNativeHandle memory = kInvalidVulkanExternalNativeHandle,
        VulkanExternalNativeHandle cuda_to_vulkan = kInvalidVulkanExternalNativeHandle,
        VulkanExternalNativeHandle vulkan_to_cuda = kInvalidVulkanExternalNativeHandle) noexcept
        : descriptors_{memory, cuda_to_vulkan, vulkan_to_cuda} {}
    ~VulkanExternalFds() {
        for (const auto descriptor : descriptors_) {
#ifdef _WIN32
            if (descriptor != nullptr) (void)::CloseHandle(descriptor);
#else
            if (descriptor >= 0) (void)::close(descriptor);
#endif
        }
    }
    VulkanExternalFds(const VulkanExternalFds&) = delete;
    VulkanExternalFds& operator=(const VulkanExternalFds&) = delete;

    VulkanExternalNativeHandle& operator[](std::size_t index) noexcept {
        return descriptors_[index];
    }
    VulkanExternalNativeHandle release(std::size_t index) noexcept {
        return std::exchange(descriptors_[index], kInvalidVulkanExternalNativeHandle);
    }

private:
    std::array<VulkanExternalNativeHandle, 3> descriptors_;
};

} // namespace chronon3d::backends::vulkan

