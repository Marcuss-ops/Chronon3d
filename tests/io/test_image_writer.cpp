#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <stb_image.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace chronon3d;


TEST_CASE("image writer detects output format from path") {
    CHECK(image_format_from_path("frame.png") == ImageFormat::Png);
    CHECK(image_format_from_path("frame.PNG") == ImageFormat::Png);
    CHECK(image_format_from_path("frame.exr") == ImageFormat::Exr);
    CHECK(image_format_from_path("frame.EXR") == ImageFormat::Exr);
    CHECK(image_format_from_path("frame.jpg") == ImageFormat::Unknown);
}

TEST_CASE("PNG export rejects a GPU-authoritative framebuffer") {
    Framebuffer fb(2, 2);
    fb.set_surface_handle(runtime::RenderSurfaceHandle{1});
    fb.mark_gpu_authoritative();
    CHECK_FALSE(save_png(fb, "output/gpu_authoritative.png"));
}

TEST_CASE("Framebuffer residency reports CPU and synchronized GPU state") {
    Framebuffer fb(2, 2);
    CHECK(fb.residency() == SurfaceResidency::CpuOnly);
    fb.set_surface_handle(runtime::RenderSurfaceHandle{1});
    CHECK(fb.residency() == SurfaceResidency::CpuAndGpu);
    fb.mark_gpu_authoritative();
    CHECK(fb.residency() == SurfaceResidency::GpuOnly);
    fb.mark_cpu_gpu_synchronized();
    CHECK(fb.residency() == SurfaceResidency::CpuAndGpu);
}

TEST_CASE("save_image rejects unknown extension") {
    Framebuffer fb(2, 2);
    CHECK_FALSE(save_image(fb, "output/test_unknown_format.jpg"));
}

TEST_CASE("PNG dither changes sub-LSB quantization deterministically") {
    Framebuffer fb(16, 1, false);
    for (i32 x = 0; x < 16; ++x)
        fb.set_pixel(x, 0, Color{0.214f, 0.214f, 0.214f, 1.0f});

    const auto plain_path = std::filesystem::temp_directory_path() /
                            "chronon_dither_plain.png";
    const auto dither_path = std::filesystem::temp_directory_path() /
                             "chronon_dither_on.png";
    ImageWriteOptions plain;
    plain.format = ImageFormat::Png;
    ImageWriteOptions dithered = plain;
    dithered.dither_8bit = true;
    REQUIRE(save_png(fb, plain_path.string(), plain));
    REQUIRE(save_png(fb, dither_path.string(), dithered));

    int plain_w = 0, plain_h = 0, plain_c = 0;
    int dither_w = 0, dither_h = 0, dither_c = 0;
    unsigned char* plain_pixels = stbi_load(
        plain_path.string().c_str(), &plain_w, &plain_h, &plain_c, 4);
    unsigned char* dither_pixels = stbi_load(
        dither_path.string().c_str(), &dither_w, &dither_h, &dither_c, 4);
    REQUIRE(plain_pixels != nullptr);
    REQUIRE(dither_pixels != nullptr);
    REQUIRE(plain_w == dither_w);
    REQUIRE(plain_h == dither_h);

    int changed = 0;
    for (int i = 0; i < plain_w * plain_h * 4; ++i) {
        const int diff = std::abs(static_cast<int>(plain_pixels[i]) -
                                  static_cast<int>(dither_pixels[i]));
        CHECK(diff <= 1);
        changed += diff != 0 ? 1 : 0;
    }
    CHECK(changed > 0);

    stbi_image_free(plain_pixels);
    stbi_image_free(dither_pixels);
    std::filesystem::remove(plain_path);
    std::filesystem::remove(dither_path);
}

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
