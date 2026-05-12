#include <doctest/doctest.h>
#include <chronon3d/renderer/framebuffer.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace chronon3d;

TEST_CASE("Image writer (PNG)") {
    Framebuffer fb(32, 32);
    fb.clear(Color::red());
    
    std::string path = "test_output.png";
    
    SUBCASE("Save PNG") {
        CHECK(save_png(fb, path));
        CHECK(std::filesystem::exists(path));
        CHECK(std::filesystem::file_size(path) > 0);
    }
    
    SUBCASE("PNG Header check") {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        
        std::vector<unsigned char> header(8);
        file.read(reinterpret_cast<char*>(header.data()), 8);
        
        // PNG Signature: 89 50 4E 47 0D 0A 1A 0A
        CHECK(header[0] == 0x89);
        CHECK(header[1] == 'P');
        CHECK(header[2] == 'N');
        CHECK(header[3] == 'G');
        
        file.close();
        std::filesystem::remove(path);
    }
}
