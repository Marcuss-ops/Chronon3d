#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <chronon3d/io/image_writer.hpp>
#include <vector>

namespace chronon3d {

bool save_png(const Framebuffer& framebuffer, const std::string& path) {
    i32 width = framebuffer.width();
    i32 height = framebuffer.height();
    
    // Convert Framebuffer (float RGBA) to byte RGBA
    std::vector<uint8_t> data(width * height * 4);
    
    for (i32 y = 0; y < height; ++y) {
        for (i32 x = 0; x < width; ++x) {
            Color c = framebuffer.get_pixel(x, y);
            usize index = (y * width + x) * 4;
            
            data[index + 0] = static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f));
            data[index + 1] = static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f));
            data[index + 2] = static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f));
            data[index + 3] = static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f));
        }
    }
    
    return stbi_write_png(path.c_str(), width, height, 4, data.data(), width * 4) != 0;
}

} // namespace chronon3d
