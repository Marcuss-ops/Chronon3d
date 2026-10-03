#pragma once

#include <chronon3d/core/types/types.hpp>
#include <string>
#include <memory>
#include <vector>

namespace chronon3d::image {

struct ImageBuffer {
    int width{0};
    int height{0};
    int channels{4};
    std::unique_ptr<u8[]> pixels;
    // Optional canonical framebuffer payload: premultiplied, linear BT.709
    // RGBA float. Used by HDR/EXR decoders so ImageCache does not round through
    // the legacy 8-bit straight-sRGB image contract.
    std::unique_ptr<float[]> linear_pixels;

    [[nodiscard]] bool has_pixels() const noexcept {
        return pixels != nullptr || linear_pixels != nullptr;
    }
};

class ImageBackend {
public:
    virtual ~ImageBackend() = default;

    virtual std::unique_ptr<ImageBuffer> load_image(const std::string& path) = 0;
};

} // namespace chronon3d::image
