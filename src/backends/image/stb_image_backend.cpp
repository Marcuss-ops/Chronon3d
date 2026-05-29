#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/image/exr_mmap.hpp>

namespace chronon3d::image {

std::unique_ptr<ImageBuffer> StbImageBackend::load_image(const std::string& path) {
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".exr") {
        return load_exr_mmap(path);
    }

    int w, h, ch;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) return nullptr;

    auto buffer = std::make_unique<ImageBuffer>();
    buffer->width = w;
    buffer->height = h;
    buffer->channels = 4;
    buffer->pixels = std::make_unique<u8[]>(static_cast<size_t>(w) * h * 4);
    std::memcpy(buffer->pixels.get(), pixels, static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);

    return buffer;
}

} // namespace chronon3d::image
