#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <cuda.h>

#include <cstdint>

namespace chronon3d::media {

enum class CudaLayerResourceKind : std::uint8_t {
    Rgba = 0,
    Nv12 = 1,
};

/// Backend-neutral CUDA address table for one GPU overlay resource.  This is
/// consumed by video compositors and intentionally does not depend on Vulkan.
struct CudaLayerResource {
    CudaLayerResourceKind kind{CudaLayerResourceKind::Rgba};
    CUdeviceptr rgba{0};
    CUdeviceptr y{0};
    CUdeviceptr uv{0};
    int pitch_bytes{0};
    int uv_pitch_bytes{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

} // namespace chronon3d::media

#endif
