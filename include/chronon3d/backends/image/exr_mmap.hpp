#pragma once

#include <chronon3d/backends/image/image_backend.hpp>
#include <string>

namespace chronon3d::image {

std::unique_ptr<ImageBuffer> load_exr_mmap(const std::string& path);

} // namespace chronon3d::image