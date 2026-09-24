#include <doctest/doctest.h>

#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/cube_lut.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <cstdint>
#include <string_view>
#include <utility>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {
constexpr std::string_view kInvert2Cube = R"CUBE(
TITLE "Invert"
LUT_3D_SIZE 2
1 1 1
0 1 1
1 0 1
0 0 1
1 1 0
0 1 0
1 0 0
0 0 0
)CUBE";
}

TEST_CASE("3D cube LUT parser and trilinear color pipeline preserve alpha") {
    const auto lut = effects::parse_cube_lut(kInvert2Cube);
    REQUIRE(lut.dimension == 2);
    REQUIRE(lut.values);
    CHECK(lut.values->size() == 8);

    Framebuffer fb(1, 1);
    fb.pixels_row(0)[0] = {0.1f, 0.2f, 0.3f, 0.4f}; // premultiplied storage
    ColorPipeline pipeline;
    pipeline.add_stage(lut);
    pipeline.apply(fb);
    const auto output = fb.get_pixel(0, 0);
    CHECK(output.r == doctest::Approx(0.3f)); // mapped straight RGB is .75, .5, .25
    CHECK(output.g == doctest::Approx(0.2f));
    CHECK(output.b == doctest::Approx(0.1f));
    CHECK(output.a == doctest::Approx(0.4f));
}

TEST_CASE("3D cube LUT parser rejects malformed and unsupported tables") {
    CHECK_THROWS((void)effects::parse_cube_lut("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n"));
    CHECK_THROWS((void)effects::parse_cube_lut("LUT_3D_SIZE 2\n0 0 0\n"));
    CHECK_THROWS((void)effects::parse_cube_lut("LUT_3D_SIZE 2\n"));
}

TEST_CASE("LutCubeFramehashCanary") {
    constexpr int width = 32, height = 32;
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    auto lut = effects::parse_cube_lut(kInvert2Cube);
    EffectStack stack;
    stack.push_back(EffectInstance{std::move(lut)});
    Framebuffer fb(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            fb.set_pixel(x, y, Color{static_cast<float>(x) / (width - 1),
                                     static_cast<float>(y) / (height - 1),
                                     static_cast<float>(x + y) / (width + height - 2), 1.0f});
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);

    const char* raw_path = std::getenv("CHRONON_LUT_CUBE_RAW");
    if (raw_path && *raw_path) {
        std::ofstream raw(raw_path, std::ios::binary | std::ios::trunc);
        REQUIRE(raw.good());
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto pixel = fb.get_pixel(x, y);
            const std::uint8_t rgba[] = {Color::linear_to_srgb8(pixel.r), Color::linear_to_srgb8(pixel.g),
                Color::linear_to_srgb8(pixel.b), static_cast<std::uint8_t>(pixel.a * 255.0f + 0.5f)};
            raw.write(reinterpret_cast<const char*>(rgba), sizeof(rgba));
        }
        REQUIRE(raw.good());
    }
    CHECK(fb.get_pixel(16, 16).r == doctest::Approx(1.0f - 16.0f / 31.0f));
}
