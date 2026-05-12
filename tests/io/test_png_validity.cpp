#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <fstream>
#include <vector>

using namespace chronon3d;

TEST_CASE("PNG Validity - Contro-test 6") {
    Framebuffer fb(100, 100);
    fb.clear(Color::red());
    
    std::string filename = "test_output_validity.png";
    bool success = save_png(fb, filename);
    
    SUBCASE("save_png returns true") {
        CHECK(success == true);
    }
    
    SUBCASE("File exists and has size > 0") {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        CHECK(file.is_open() == true);
        CHECK(file.tellg() > 0);
    }
    
    SUBCASE("PNG Header is valid") {
        std::ifstream file(filename, std::ios::binary);
        unsigned char header[8];
        file.read(reinterpret_cast<char*>(header), 8);
        
        // PNG Header: 89 50 4E 47 0D 0A 1A 0A
        CHECK(header[0] == 0x89);
        CHECK(header[1] == 0x50); // P
        CHECK(header[2] == 0x4E); // N
        CHECK(header[3] == 0x47); // G
        CHECK(header[4] == 0x0D);
        CHECK(header[5] == 0x0A);
        CHECK(header[6] == 0x1A);
        CHECK(header[7] == 0x0A);
    }
}
