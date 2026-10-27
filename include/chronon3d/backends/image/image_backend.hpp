#pragma once

#include <chronon3d/core/types/types.hpp>
#include <string>
#include <memory>
#include <vector>
#include <span>
#include <cstddef>

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

    /// Decode an encoded image payload without materializing it on disk.
    /// Backends that do not support memory input fail closed by returning
    /// nullptr; the software STB backend implements PNG/JPEG memory decoding.
    virtual std::unique_ptr<ImageBuffer> load_image_from_memory(
        std::span<const std::byte>) { return nullptr; }
};

} // namespace chronon3d::image
