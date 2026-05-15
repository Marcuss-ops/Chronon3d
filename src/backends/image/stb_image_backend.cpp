#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <chronon3d/backends/image/stb_image_backend.hpp>

namespace chronon3d::image {

std::unique_ptr<ImageBuffer> StbImageBackend::load_image(const std::string& path) {
    int w, h, ch;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) return nullptr;

    auto buffer = std::make_unique<ImageBuffer>();
    buffer->width = w;
    buffer->height = h;
    buffer->channels = 4;
    buffer->pixels = std::unique_ptr<u8[]>(pixels);

    return buffer;
}

} // namespace chronon3d::image
