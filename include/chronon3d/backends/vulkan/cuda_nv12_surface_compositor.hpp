#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/backends/vulkan/cuda_yuv_conversion.hpp>

#include <cuda.h>

#include <cstdint>
#include <memory>
#include <span>

namespace chronon3d::backends::vulkan {

enum class CudaYuvFormat : std::uint8_t {
    Nv12,
    P010,
};

/// CUDA address table for the resources referenced by a GpuLayerBatch.
/// `rgba` points to device-local float4 pixels; no host pixel storage is
/// involved in the direct YUV path.  The table is indexed by
/// LayerInstance::resource_index.
struct CudaLayerResource {
    CUdeviceptr rgba{0};
    int pitch_bytes{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

/// Owns CUDA kernels for NV12/RGBA interop without host staging. The bridge
/// imports one Vulkan RGBA surface and owns the external memory/semaphore
/// handles; the compositor writes CUDA NV12 planes directly when requested.
class CudaNv12SurfaceCompositor final {
public:
    /// Compiles and loads the NVRTC module once on the supplied CUDA
    /// context. This validates the driver/context before NVENC is opened.
    static void warm_up(CUcontext context);

    CudaNv12SurfaceCompositor(
        const CudaExternalMemoryInfo& target,
        CUcontext context);
    /// Direct-YUV-only variant.  It owns the CUDA module/stream but no
    /// Vulkan external surface; direct NV12 batch kernels use linear CUDA
    /// overlay resources and therefore do not need an RGBA bridge.
    explicit CudaNv12SurfaceCompositor(CUcontext context);
    ~CudaNv12SurfaceCompositor();

    CudaNv12SurfaceCompositor(const CudaNv12SurfaceCompositor&) = delete;
    CudaNv12SurfaceCompositor& operator=(const CudaNv12SurfaceCompositor&) = delete;

    /// Completes CUDA work associated with this imported surface before its
    /// external memory and semaphore handles are retired.
    [[nodiscard]] bool synchronize() noexcept;

    bool composite(CUdeviceptr y, int y_pitch, CUdeviceptr uv, int uv_pitch,
                   std::uint32_t width, std::uint32_t height, CUstream stream,
                   CudaYuvFormat format = CudaYuvFormat::Nv12,
                   YuvToRgbParams params = {});

    /// Direct NV12 overlay compositor without intermediate RGB surfaces
    bool composite_direct_nv12(
        CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
        CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
        std::uint32_t width, std::uint32_t height, CUstream stream);

    /// Executes a backend-neutral layer batch directly into NV12.  The
    /// compositor evaluates the batch in painter order and owns one 2x2
    /// invocation per luma/chroma block; it never allocates an RGBA overlay.
    /// Only Normal and Add are legal on this fast path. Effects and blend
    /// modes outside that contract must be routed through FullRgb.
    bool composite_direct_nv12_batch(
        const runtime::GpuLayerBatch& batch,
        std::span<const CudaLayerResource> resources,
        CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
        CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
        std::uint32_t width, std::uint32_t height, CUstream stream);

    /// Converts the already-composited imported RGBA surface directly into
    /// CUDA NV12 planes. This is the native encoder handoff: no intermediate
    /// RGBA CUDA allocation or device-to-device staging copy is created.
    bool composite_surface_to_nv12(
        CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
        std::uint32_t width, std::uint32_t height, CUstream stream);

private:
    CUcontext context_{nullptr};
    CUmodule module_{nullptr};
    CUfunction kernel_{nullptr};
    CUfunction p010_kernel_{nullptr};
    CUfunction direct_nv12_kernel_{nullptr};
    CUfunction direct_nv12_batch_kernel_{nullptr};
    CUfunction rgba_to_nv12_kernel_{nullptr};
    CUfunction rgba_u8_to_nv12_kernel_{nullptr};
    CUdeviceptr layer_batch_buffer_{0};
    std::size_t layer_batch_capacity_{0};
    CUstream stream_{nullptr};
    bool first_write_{true};
    bool surface_u8_{false};
    std::unique_ptr<CudaVulkanSurfaceBridge> bridge_;
};

} // namespace chronon3d::backends::vulkan

#endif
