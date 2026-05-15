#pragma once

#include <chronon3d/core/types.hpp>
#include <string>
#include <memory>
#include <vector>

namespace chronon3d::image {

struct ImageBuffer {
    int width{0};
    int height{0};
    int channels{4};
    std::unique_ptr<u8[]> pixels;
};

class ImageBackend {
public:
    virtual ~ImageBackend() = default;

    virtual std::unique_ptr<ImageBuffer> load_image(const std::string& path) = 0;
};

} // namespace chronon3d::image
