#pragma once

#include <chronon3d/runtime/render_surface.hpp>

#include <cstdint>

namespace chronon3d::runtime {

/// The only CUDA/Vulkan external-surface lanes.  RGBA is copied or aliased
/// into the native encoder surface; NV12/P010 are DirectYUV surfaces and must
/// never enter the RGBA copy function.
enum class CudaSurfaceLane : std::uint8_t {
    Invalid,
    RgbaCopy,
    Nv12DirectYuv,
    P010DirectYuv,
};

struct CudaSurfaceContract {
    CudaSurfaceLane lane{CudaSurfaceLane::Invalid};
    const char* reason{"invalid CUDA surface description"};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return lane != CudaSurfaceLane::Invalid;
    }
};

[[nodiscard]] constexpr CudaSurfaceContract
validate_cuda_surface_contract(const SurfaceDesc& desc) noexcept {
    if (desc.width == 0 || desc.height == 0) {
        return {CudaSurfaceLane::Invalid, "CUDA surface extent is empty"};
    }

    if ((desc.format == PixelFormat::Rgba32Float ||
         desc.format == PixelFormat::Rgba8Unorm)) {
        if (desc.kind != ResourceKind::Color) {
            return {CudaSurfaceLane::Invalid,
                    "RGBA CUDA surface must be ResourceKind::Color"};
        }
        return {CudaSurfaceLane::RgbaCopy, "rgba_copy"};
    }

    if (desc.format == PixelFormat::Nv12 ||
        desc.format == PixelFormat::P010) {
        if (desc.kind != ResourceKind::Yuv) {
            return {CudaSurfaceLane::Invalid,
                    "NV12/P010 CUDA surface must be ResourceKind::Yuv"};
        }
        if ((desc.width & 1u) != 0 || (desc.height & 1u) != 0) {
            return {CudaSurfaceLane::Invalid,
                    "NV12/P010 CUDA surface requires even dimensions"};
        }
        return desc.format == PixelFormat::Nv12
            ? CudaSurfaceContract{CudaSurfaceLane::Nv12DirectYuv, "nv12_direct_yuv"}
            : CudaSurfaceContract{CudaSurfaceLane::P010DirectYuv, "p010_direct_yuv"};
    }

    return {CudaSurfaceLane::Invalid,
            "CUDA surface format is outside the certified lanes"};
}

} // namespace chronon3d::runtime
