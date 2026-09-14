#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/media/video/cuda_layer_resource.hpp>
#include <chronon3d/backends/vulkan/cuda_yuv_conversion.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <cuda.h>

#include <cstdint>
#include <memory>
#include <span>

namespace chronon3d::media::video {

using CudaExternalMemoryInfo = ::chronon3d::backends::vulkan::CudaExternalMemoryInfo;
using CudaVulkanSurfaceBridge = ::chronon3d::backends::vulkan::CudaVulkanSurfaceBridge;
using YuvToRgbParams = ::chronon3d::backends::vulkan::YuvToRgbParams;

enum class CudaYuvFormat : std::uint8_t {
    Nv12,
    P010,
};

/// CUDA address table for the resources referenced by a GpuLayerBatch.
/// `rgba` points to device-local pixels; ordinary RGBA resources use float4
/// texels while MTSDF resources use the bit-preserving RGBA8-in-a-float-cell
/// atlas ABI shared with Vulkan. No host pixel storage is involved in the
/// direct YUV path. The table is indexed by LayerInstance::resource_index.
using CudaLayerResource = ::chronon3d::media::CudaLayerResource;

/// Owns CUDA kernels for NV12/RGBA interop without host staging. The bridge
/// imports one Vulkan RGBA surface and owns the external memory/semaphore
/// handles; the compositor writes CUDA NV12 planes directly when requested.
class CudaNv12Compositor final {
public:
    /// Compiles and loads the NVRTC module once on the supplied CUDA
    /// context. This validates the driver/context before NVENC is opened.
    static void warm_up(CUcontext context);

    CudaNv12Compositor(
        const CudaExternalMemoryInfo& target,
        CUcontext context);
    /// Direct-YUV-only variant.  It owns the CUDA module/stream but no
    /// Vulkan external surface; direct NV12 batch kernels use linear CUDA
    /// overlay resources and therefore do not need an RGBA bridge.
    explicit CudaNv12Compositor(CUcontext context);
    ~CudaNv12Compositor();

    CudaNv12Compositor(const CudaNv12Compositor&) = delete;
    CudaNv12Compositor& operator=(const CudaNv12Compositor&) = delete;

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
        std::uint32_t width, std::uint32_t height, CUstream stream,
        CudaYuvFormat format = CudaYuvFormat::Nv12);

    /// Transfer encode applied at the RGBA surface → NV12 boundary.
    ///
    /// The canonical graphics surface is RGBA32F with a LINEAR transfer
    /// (BT.709 primaries, premultiplied alpha); the NVENC/P010 SDR target is
    /// nonlinear. The OETF therefore runs here, chosen by the TARGET, and is
    /// deliberately not inferred from the surface: assuming "already encoded"
    /// is what made every natively drawn image/colour/text layer render in
    /// linear light (mid-grey 128 arrived as 54).
    enum TransferEncode : std::uint32_t {
        TransferNone = 0,  ///< target is linear — no encode required
        TransferSrgb = 1,  ///< sRGB / BT.709 SDR OETF (the encoder default)
    };

    /// Converts the already-composited imported RGBA surface directly into
    /// CUDA NV12 planes. This is the native encoder handoff: no intermediate
    /// RGBA CUDA allocation or device-to-device staging copy is created.
    /// A surface exported as 8-bit already carries the encoder's domain, so
    /// the transfer encode is applied only to the RGBA32F (linear) surface.
    ///
    /// `dirty_regions` requests a partial conversion. That is honoured only
    /// when `destination_retains_previous_frame` proves the untouched pixels
    /// are already this frame's (see media/video/nv12_region_policy.hpp); a
    /// pooled encoder frame does not provide that proof, so callers on that
    /// path pass no regions and the whole frame is converted.
    bool composite_surface_to_nv12(
        CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
        std::uint32_t width, std::uint32_t height, CUstream stream,
        std::span<const raster::BBox> dirty_regions = {},
        std::uint32_t transfer_encode = TransferSrgb,
        bool destination_retains_previous_frame = false);

private:
    CUcontext context_{nullptr};
    // Keep the loaded module alive for this compositor's CUfunction handles.
    // The PTX source artifact belongs to ProgramCache; this device/context
    // lease is intentionally session-local and cannot outlive the context.
    std::shared_ptr<const void> module_lease_;
    CUmodule module_{nullptr};
    CUfunction kernel_{nullptr};
    CUfunction p010_kernel_{nullptr};
    CUfunction direct_nv12_kernel_{nullptr};
    CUfunction direct_nv12_batch_kernel_{nullptr};
    CUfunction direct_p010_batch_kernel_{nullptr};
    CUfunction rgba_to_nv12_kernel_{nullptr};
    CUfunction rgba_u8_to_nv12_kernel_{nullptr};
    // AUDIT-02A/02B: per-slot pinned host staging + device buffers in a
    // ping-pong pair. Host storage is allocated once (pinned, never per
    // frame); the two device slots alternate so a caller that switches
    // streams between frames can never overwrite a device slot whose
    // H2D copy + kernel read is still in flight on the previous stream.
    static constexpr int kLayerBatchSlots = 2;
    CUdeviceptr layer_batch_buffer_[kLayerBatchSlots]{0, 0};
    std::size_t layer_batch_capacity_dev_[kLayerBatchSlots]{0, 0};
    CUdeviceptr layer_batch_host_[kLayerBatchSlots]{0, 0};
    std::size_t layer_batch_capacity_[kLayerBatchSlots]{0, 0};
    int layer_batch_slot_{0};
    CUstream stream_{nullptr};
    bool first_write_{true};
    bool surface_u8_{false};
    std::unique_ptr<CudaVulkanSurfaceBridge> bridge_;
};

} // namespace chronon3d::media::video

#endif
