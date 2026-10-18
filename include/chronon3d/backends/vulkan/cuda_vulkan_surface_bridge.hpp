#pragma once

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#include <functional>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <cstdint>
#include <memory>

namespace chronon3d::backends::vulkan {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

/// Imports one Vulkan-exported RGBA surface (float4 or uchar4) into CUDA.
///
/// The imported image remains device-local. The two external semaphores are
/// the ownership boundary: wait_for_vulkan() must complete before CUDA writes,
/// and signal_for_vulkan() is the release operation that lets the next Vulkan
/// submission sample the image. Timeline handles are used when the Vulkan
/// export advertises them; binary handles remain a compatibility fallback.
class CudaVulkanSurfaceBridge final {
public:
    CudaVulkanSurfaceBridge(const CudaExternalMemoryInfo& info,
                            CUcontext context,
                            CUstream stream = nullptr);
    ~CudaVulkanSurfaceBridge();

    CudaVulkanSurfaceBridge(const CudaVulkanSurfaceBridge&) = delete;
    CudaVulkanSurfaceBridge& operator=(const CudaVulkanSurfaceBridge&) = delete;

    [[nodiscard]] CUarray array() const noexcept { return m_array; }
    [[nodiscard]] CUsurfObject surface_object() const noexcept {
        return m_surface;
    }
    [[nodiscard]] CUexternalMemory external_memory() const noexcept {
        return m_memory;
    }
    [[nodiscard]] CUexternalSemaphore cuda_to_vulkan_semaphore() const noexcept {
        return m_cuda_to_vulkan;
    }
    [[nodiscard]] CUexternalSemaphore vulkan_to_cuda_semaphore() const noexcept {
        return m_vulkan_to_cuda;
    }

    /// Waits for Vulkan's previous write before a CUDA compositor accesses it.
    void wait_for_vulkan(CUstream stream = nullptr);

    /// Signals Vulkan after CUDA's compositor has finished writing the image.
    void signal_for_vulkan(CUstream stream = nullptr);

private:
    void make_current();
    CUcontext m_context{nullptr};
    CUexternalMemory m_memory{nullptr};
    CUmipmappedArray m_mipmapped{nullptr};
    CUarray m_array{nullptr};
    CUsurfObject m_surface{0};
    CUexternalSemaphore m_cuda_to_vulkan{nullptr};
    CUexternalSemaphore m_vulkan_to_cuda{nullptr};
    CUstream m_stream{nullptr};
    bool m_timeline{false};
    std::shared_ptr<InteropGeneration> m_generation{};
    std::function<std::uint64_t()> m_wait_vulkan_fence{};
};

#endif

}  // namespace chronon3d::backends::vulkan
