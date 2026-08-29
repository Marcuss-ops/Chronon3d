#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/media/video/cuda_layer_resource.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>

#include <cuda.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace chronon3d::media {

/// CUDA-only NV12 compositor for Direct-YUV. This interface has no Vulkan
/// surface, bridge, or RenderBackend dependency.
class CudaDirectNv12Compositor final {
public:
    explicit CudaDirectNv12Compositor(CUcontext context);
    ~CudaDirectNv12Compositor();

    CudaDirectNv12Compositor(const CudaDirectNv12Compositor&) = delete;
    CudaDirectNv12Compositor& operator=(const CudaDirectNv12Compositor&) = delete;

    bool composite_direct_nv12_batch(
        const runtime::GpuLayerBatch& batch,
        std::span<const CudaLayerResource> resources,
        CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
        CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
        std::uint32_t width, std::uint32_t height, CUstream stream);

private:
    CUcontext context_{nullptr};
    CUmodule module_{nullptr};
    CUfunction kernel_{nullptr};
    CUdeviceptr layer_batch_buffer_{0};
    std::size_t layer_batch_capacity_{0};
    CUstream stream_{nullptr};
};

} // namespace chronon3d::media

#endif
