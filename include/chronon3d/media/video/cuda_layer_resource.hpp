#pragma once

#include <cstdint>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::media {

enum class CudaLayerResourceKind : std::uint8_t {
    Rgba = 0,
    Nv12 = 1,
    Mtsdf = 2,
    /// RGBA32F in the canonical compositor domain: linear and premultiplied.
    /// The direct YUV kernel unpremultiplies it before applying the target
    /// transfer function and YUV matrix.
    RgbaLinearPremultiplied = 3,
};

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

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
    float distance_range_px{8.0f};
    std::uint32_t color{0xFFFFFFFFu};
    std::uint32_t stroke_color{0};
    float stroke_width{0.0f};
    /// Canonical single drop shadow of a prepared MTSDF text run. The colour
    /// packs linear premultiplied-by-alpha RGB plus the authored colour alpha;
    /// `shadow_opacity` stays a separate factor exactly like the canonical
    /// text renderer, and `shadow_dx/dy/blur` are destination pixels. A layer
    /// without an enabled shadow carries `shadow_color == 0`.
    std::uint32_t shadow_color{0};
    float shadow_opacity{0.0f};
    float shadow_dx{0.0f};
    float shadow_dy{0.0f};
    float shadow_blur{0.0f};
};

#endif

} // namespace chronon3d::media
