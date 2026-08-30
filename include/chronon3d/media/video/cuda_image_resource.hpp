#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <cuda.h>
#include <chronon3d/runtime/gpu_runtime.hpp>

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
    // Keeps the owning CUDA primary context alive until the last resource
    // reference is released, including cross-thread destruction.
    std::shared_ptr<runtime::GpuRuntime> owner_gpu;

    CudaImageResource() = default;
    CudaImageResource(const CudaImageResource&) = delete;
    CudaImageResource& operator=(const CudaImageResource&) = delete;

    ~CudaImageResource() {
        if (!ptr) return;
        const auto context = owner_gpu
            ? reinterpret_cast<CUcontext>(owner_gpu->native_context_handle())
            : nullptr;
        CUcontext previous = nullptr;
        if (context) (void)cuCtxGetCurrent(&previous);
        if (context && previous != context) (void)cuCtxSetCurrent(context);
        (void)cuMemFree(ptr);
        if (context && previous && previous != context) (void)cuCtxSetCurrent(previous);
        ptr = 0;
    }
};

} // namespace chronon3d::media

#endif
