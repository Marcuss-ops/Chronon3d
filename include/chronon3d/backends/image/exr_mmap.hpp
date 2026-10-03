#pragma once

#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d::image {

#ifdef CHRONON3D_ENABLE_EXR
std::unique_ptr<ImageBuffer> load_exr_mmap(const std::string& path);
/// Load an EXR directly into the engine's linear-float framebuffer without
/// passing through ImageBuffer's legacy 8-bit representation.
std::shared_ptr<Framebuffer> load_exr_as_framebuffer_float(const std::string& path);
/// Read the optional OpenEXR whiteLuminance attribute (nits for RGB 1.0).
/// Throws when the attribute exists but is not finite and positive.
std::optional<float> read_exr_white_luminance_nits(const std::string& path);
#else
inline std::unique_ptr<ImageBuffer> load_exr_mmap(const std::string& path) {
    (void)path;
    return nullptr;
}
inline std::shared_ptr<Framebuffer> load_exr_as_framebuffer_float(const std::string&) {
    return nullptr;
}
inline std::optional<float> read_exr_white_luminance_nits(const std::string&) {
    return std::nullopt;
}
#endif

} // namespace chronon3d::image
