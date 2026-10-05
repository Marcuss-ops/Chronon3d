#pragma once

#include <array>
#include <cstddef>
#include <unistd.h>
#include <utility>

namespace chronon3d::backends::vulkan {

// Scoped ownership for the external-memory + two-semaphore handoff. CUDA
// consumes each descriptor only after a successful import; Vulkan exports
// transfer all three together only once the complete result is constructed.
class VulkanExternalFds {
public:
    explicit VulkanExternalFds(int memory = -1, int cuda_to_vulkan = -1,
                               int vulkan_to_cuda = -1) noexcept
        : descriptors_{memory, cuda_to_vulkan, vulkan_to_cuda} {}
    ~VulkanExternalFds() {
        for (const int descriptor : descriptors_)
            if (descriptor >= 0) (void)::close(descriptor);
    }
    VulkanExternalFds(const VulkanExternalFds&) = delete;
    VulkanExternalFds& operator=(const VulkanExternalFds&) = delete;

    int& operator[](std::size_t index) noexcept { return descriptors_[index]; }
    int release(std::size_t index) noexcept {
        return std::exchange(descriptors_[index], -1);
    }

private:
    std::array<int, 3> descriptors_;
};

} // namespace chronon3d::backends::vulkan
