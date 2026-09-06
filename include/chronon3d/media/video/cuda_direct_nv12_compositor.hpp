#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/media/video/cuda_layer_resource.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>

#include <cuda.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace chronon3d::media {

/// Host-side mirror of the DirectYuvLayer CUDA kernel parameter struct.
/// One entry per composited layer instance; the compositor copies a packed
/// array of these to device memory per batch. Defined here (not in the .cpp)
/// so the compositor can own a reusable scratch vector of them and keep the
/// DirectYUV hot path free of per-frame heap allocations.
struct alignas(8) DirectYuvLayerHost {
    int kind{0};
    CUdeviceptr rgba{0};
    CUdeviceptr y{0};
    CUdeviceptr uv{0};
    int pitch{0};
    int uv_pitch{0};
    int source_width{0};
    int source_height{0};
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{0.0f}, dst_y1{0.0f};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};
    float opacity{1.0f};
    int blend_mode{0};
    float corner_radius{0.0f};
    float _pad{0.0f};
};
static_assert(sizeof(DirectYuvLayerHost) == 96);

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
    // Reusable host-side scratch for the per-call layer array. Mirroring the
    // device-side layer_batch_buffer_ capacity reuse keeps the DirectYUV hot
    // path free of a fresh heap allocation per composited frame. The
    // compositor owns its CUDA context/stream and is used by one render
    // session at a time, so the scratch needs no separate locking.
    std::vector<DirectYuvLayerHost> host_layers_scratch_;
};

} // namespace chronon3d::media

#endif
