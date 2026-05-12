#include <chronon3d/renderer/framebuffer.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <chronon3d/math/color.hpp>
#include <iostream>
#include <filesystem>

using namespace chronon3d;

int main() {
    const int size = 256;
    const int tileSize = 32;
    Framebuffer fb(size, size);
    
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool isWhite = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            if (isWhite) {
                fb.set_pixel(x, y, Color{1, 1, 1, 1});
            } else {
                fb.set_pixel(x, y, Color{0.2f, 0.4f, 0.8f, 1});
            }
        }
    }
    
    std::filesystem::create_directories("assets/images");
    if (save_png(fb, "assets/images/checker.png")) {
        std::cout << "Generated assets/images/checker.png" << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to generate checker.png" << std::endl;
        return 1;
    }
}
