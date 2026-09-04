#include <doctest/doctest.h>

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

#include <stb_image.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>

using namespace chronon3d;

TEST_CASE("PNG writer produces a valid, pixel-perfect white roundtrip") {
    const std::filesystem::path output_dir = "output/tests/io";
    const std::filesystem::path path = output_dir / "png_roundtrip_white.png";
    std::filesystem::create_directories(output_dir);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    constexpr i32 width = 64;
    constexpr i32 height = 64;
    Framebuffer fb(width, height);
    fb.clear(Color::white());

    REQUIRE(save_png(fb, path.string()));
    REQUIRE(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 8);

    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    std::array<unsigned char, 8> header{};
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    REQUIRE(file.gcount() == static_cast<std::streamsize>(header.size()));
    const std::array<unsigned char, 8> expected{
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(header == expected);
    file.close();

    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_channels = 0;
    unsigned char* pixels = stbi_load(
        path.string().c_str(),
        &decoded_width,
        &decoded_height,
        &decoded_channels,
        4);
    INFO("stbi_load failure: ", stbi_failure_reason() ? stbi_failure_reason() : "none");
    REQUIRE(pixels != nullptr);
    REQUIRE(decoded_width == width);
    REQUIRE(decoded_height == height);

    std::size_t bad_pixels = 0;
    const std::size_t pixel_count =
        static_cast<std::size_t>(decoded_width) * static_cast<std::size_t>(decoded_height);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const std::size_t offset = pixel * 4;
        if (pixels[offset + 0] != 255 ||
            pixels[offset + 1] != 255 ||
            pixels[offset + 2] != 255 ||
            pixels[offset + 3] != 255) {
            ++bad_pixels;
        }
    }
    stbi_image_free(pixels);

    CHECK(bad_pixels == 0);
    std::filesystem::remove(path, ec);
}
