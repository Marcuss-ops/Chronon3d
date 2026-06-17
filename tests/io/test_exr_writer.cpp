#include <doctest/doctest.h>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <filesystem>
using namespace chronon3d;


TEST_CASE("save_exr writes RGBA EXR file (float)") {
    const std::string path = "output/tests/io/test_frame_float.exr";

    Framebuffer fb(4, 3);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    fb.set_pixel(1, 1, Color{1.0f, 0.5f, 0.25f, 0.75f});

    CHECK(save_image(fb, path));
    CHECK(std::filesystem::exists(path));

    Imf::InputFile file(path.c_str());
    const Imf::Header& header = file.header();

    CHECK(header.channels().findChannel("R") != nullptr);
    CHECK(header.channels().findChannel("G") != nullptr);
    CHECK(header.channels().findChannel("B") != nullptr);
    CHECK(header.channels().findChannel("A") != nullptr);

    // Default format is FLOAT
    auto dw = header.dataWindow();
    const int width = dw.max.x - dw.min.x + 1;
    const int height = dw.max.y - dw.min.y + 1;

    CHECK(width == 4);
    CHECK(height == 3);
}

TEST_CASE("save_exr writes RGBA EXR file (half float)") {
    const std::string path = "output/tests/io/test_frame_half.exr";

    Framebuffer fb(4, 3);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    fb.set_pixel(1, 1, Color{1.0f, 0.5f, 0.25f, 0.75f});

    ImageWriteOptions opts;
    opts.format = ImageFormat::Exr;
    opts.exr_half_float = true;

    CHECK(save_image(fb, path, opts));
    CHECK(std::filesystem::exists(path));

    Imf::InputFile file(path.c_str());
    const Imf::Header& header = file.header();

    CHECK(header.channels().findChannel("R") != nullptr);
    CHECK(header.channels().findChannel("G") != nullptr);
    CHECK(header.channels().findChannel("B") != nullptr);
    CHECK(header.channels().findChannel("A") != nullptr);

    // Verify channels use HALF pixel type
    const Imf::Channel* rch = header.channels().findChannel("R");
    REQUIRE(rch != nullptr);
    CHECK(rch->type == Imf::HALF);

    auto dw = header.dataWindow();
    const int width = dw.max.x - dw.min.x + 1;
    const int height = dw.max.y - dw.min.y + 1;

    CHECK(width == 4);
    CHECK(height == 3);

    // Half-float file should be roughly half the size of float (4 channels × 2 bytes vs 4 bytes)
    std::filesystem::remove(path);
}

TEST_CASE("save_exr writes tiled RGBA EXR file with DWAA compression") {
    const std::string path = "output/tests/io/test_frame_tiled_dwaa.exr";

    Framebuffer fb(512, 512);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 1.0f});

    ImageWriteOptions opts;
    opts.format = ImageFormat::Exr;
    opts.exr_half_float = true;
    opts.exr_tiled = true;
    opts.exr_dwaa = true;

    CHECK(save_image(fb, path, opts));
    CHECK(std::filesystem::exists(path));

    Imf::InputFile file(path.c_str());
    const Imf::Header& header = file.header();

    CHECK(header.channels().findChannel("R") != nullptr);
    CHECK(header.channels().findChannel("G") != nullptr);
    CHECK(header.channels().findChannel("B") != nullptr);
    CHECK(header.channels().findChannel("A") != nullptr);

    CHECK(header.hasTileDescription());
    CHECK(header.compression() == Imf::DWAA_COMPRESSION);

    std::filesystem::remove(path);
}
