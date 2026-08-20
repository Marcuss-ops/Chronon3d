#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>

#include <cuda.h>

#include <cstdint>
#include <memory>

namespace chronon3d::backends::vulkan {

/// Converts an NV12 AVHWFramesContext surface into an imported Vulkan RGBA
/// surface without staging through host memory. The bridge owns the external
/// memory/semaphore handles; this class only owns the CUDA kernel resources.
class CudaNv12SurfaceCompositor final {
public:
    CudaNv12SurfaceCompositor(
        const CudaExternalMemoryInfo& target,
        CUcontext context);
    ~CudaNv12SurfaceCompositor();

    CudaNv12SurfaceCompositor(const CudaNv12SurfaceCompositor&) = delete;
    CudaNv12SurfaceCompositor& operator=(const CudaNv12SurfaceCompositor&) = delete;

    bool composite(CUdeviceptr y, int y_pitch, CUdeviceptr uv, int uv_pitch,
                   std::uint32_t width, std::uint32_t height, CUstream stream);

private:
    CUcontext context_{nullptr};
    CUmodule module_{nullptr};
    CUfunction kernel_{nullptr};
    CUstream stream_{nullptr};
    bool first_write_{true};
    std::unique_ptr<CudaVulkanSurfaceBridge> bridge_;
};

} // namespace chronon3d::backends::vulkan

#endif
