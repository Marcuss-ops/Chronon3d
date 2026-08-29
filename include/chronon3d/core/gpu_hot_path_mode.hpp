#pragma once

#include <cstdint>
#include <string_view>

namespace chronon3d {

enum class GpuHotPathMode : std::uint8_t {
    Auto = 0,
    RequireGpuNative = 1,
    RequireDirectYuv = 2,
};

[[nodiscard]] constexpr std::string_view to_string(GpuHotPathMode mode) noexcept {
    switch (mode) {
        case GpuHotPathMode::Auto: return "auto";
        case GpuHotPathMode::RequireGpuNative: return "require_gpu_native";
        case GpuHotPathMode::RequireDirectYuv: return "require_direct_yuv";
    }
    return "auto";
}

[[nodiscard]] inline GpuHotPathMode parse_gpu_hot_path_mode(std::string_view str) noexcept {
    if (str == "require_gpu_native" || str == "gpu_native" || str == "native") {
        return GpuHotPathMode::RequireGpuNative;
    }
    if (str == "require_direct_yuv" || str == "direct_yuv" || str == "yuv") {
        return GpuHotPathMode::RequireDirectYuv;
    }
    return GpuHotPathMode::Auto;
}

} // namespace chronon3d
