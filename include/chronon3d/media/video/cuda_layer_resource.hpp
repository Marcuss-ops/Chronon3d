#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <cuda.h>

#include <cstdint>

namespace chronon3d::media {

/// Backend-neutral CUDA address table for one GPU overlay resource.  This is
/// consumed by video compositors and intentionally does not depend on Vulkan.
struct CudaLayerResource {
    CUdeviceptr rgba{0};
    int pitch_bytes{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

} // namespace chronon3d::media

#endif
