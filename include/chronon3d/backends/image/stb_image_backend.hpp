#pragma once

#include <chronon3d/backends/image/image_backend.hpp>

namespace chronon3d::image {

class StbImageBackend final : public ImageBackend {
public:
    ~StbImageBackend() override = default;

    std::unique_ptr<ImageBuffer> load_image(const std::string& path) override;
};

} // namespace chronon3d::image
