#pragma once

#include <chronon3d/backends/image/image_backend.hpp>
#include <string>

namespace chronon3d::image {

#ifdef CHRONON3D_ENABLE_EXR
std::unique_ptr<ImageBuffer> load_exr_mmap(const std::string& path);
#else
inline std::unique_ptr<ImageBuffer> load_exr_mmap(const std::string& path) {
    (void)path;
    return nullptr;
}
#endif

} // namespace chronon3d::image