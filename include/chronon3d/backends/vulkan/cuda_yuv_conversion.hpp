#pragma once

#include <chronon3d/runtime/render_surface.hpp>

#include <cstdint>

namespace chronon3d::backends::vulkan {

/// Scalar coefficients consumed by CUDA YUV loaders and conversion kernels.
/// Values are expressed in the source sample domain and normalized RGB domain.
struct YuvToRgbParams {
    float y_offset{16.0f};
    float y_scale{1.0f / 219.0f};
    float uv_offset{128.0f};
    float uv_scale{1.0f / 224.0f};
    float r_v{1.5748f};
    float g_u{-0.1873f};
    float g_v{-0.4681f};
    float b_u{1.8556f};
    std::uint32_t storage_shift{0};
};

[[nodiscard]] constexpr YuvToRgbParams make_yuv_to_rgb_params(
    runtime::ColorMetadata metadata,
    runtime::PixelFormat format) noexcept {
    const bool p010 = format == runtime::PixelFormat::P010;
    const float scale = p010 ? 4.0f : 1.0f;
    YuvToRgbParams params;
    params.storage_shift = p010 ? 6u : 0u;
    params.y_offset = (metadata.range == runtime::ColorRange::Full)
        ? 0.0f : 16.0f * scale;
    params.y_scale = 1.0f / ((metadata.range == runtime::ColorRange::Full)
        ? 255.0f * scale : 219.0f * scale);
    params.uv_offset = 128.0f * scale;
    params.uv_scale = 1.0f / (224.0f * scale);

    switch (metadata.matrix) {
        case runtime::ColorMatrix::Bt601:
            params.r_v = 1.5960268f;
            params.g_u = -0.3917623f;
            params.g_v = -0.8129676f;
            params.b_u = 2.0172321f;
            break;
        case runtime::ColorMatrix::Bt2020Ncl:
            params.r_v = 1.4746f;
            params.g_u = -0.16455f;
            params.g_v = -0.57135f;
            params.b_u = 1.8814f;
            break;
        case runtime::ColorMatrix::Identity:
        case runtime::ColorMatrix::Bt709:
        default:
            params.r_v = 1.5748f;
            params.g_u = -0.1873f;
            params.g_v = -0.4681f;
            params.b_u = 1.8556f;
            break;
    }
    return params;
}

} // namespace chronon3d::backends::vulkan
