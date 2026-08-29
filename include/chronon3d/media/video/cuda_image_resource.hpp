#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <cuda.h>

#include <cstddef>
#include <cstdint>

namespace chronon3d::media {

/// Device-resident RGBA image owned by a Direct-YUV program template.
///
/// Typed replacement for the old `std::shared_ptr<void> resources_owner`:
/// the address table the compositor consumes plus deterministic CUDA
/// teardown on destruction.  Non-copyable — ownership flows through
/// shared_ptr inside DirectYuvTemplate.
struct CudaImageResource {
    CUdeviceptr ptr{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::size_t pitch_bytes{0};

    CudaImageResource() = default;
    CudaImageResource(const CudaImageResource&) = delete;
    CudaImageResource& operator=(const CudaImageResource&) = delete;

    ~CudaImageResource() {
        if (ptr) (void)cuMemFree(ptr);
    }
};

} // namespace chronon3d::media

#endif