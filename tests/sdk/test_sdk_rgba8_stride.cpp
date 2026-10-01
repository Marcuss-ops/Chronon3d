#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstddef>

namespace {

chronon3d::Composition one_pixel_color(float r, float g, float b, float a) {
    return chronon3d::Composition{
        chronon3d::CompositionSpec{
            .name = "sdk_rgba8_stride",
            .width = 5,
            .height = 2,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [=](const chronon3d::FrameContext&) -> chronon3d::Scene {
            chronon3d::SceneBuilder builder;
            builder.layer("pixel", [=](chronon3d::LayerBuilder& layer) {
                layer.screen_dimensions(5.0f, 2.0f)
                 .fill(chronon3d::Color{r, g, b, a});
            });
            return builder.build();
        },
    };
}

}  // namespace

TEST_CASE("SDK RGBA8 conversion walks logical rows and emits straight sRGB pixels") {
    chronon3d::sdk::RenderSettings settings{};
    settings.width = 5;
    settings.height = 2;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    chronon3d::sdk::RenderEngine engine{settings};

    const auto result = engine.render(
        one_pixel_color(0.25f, 0.0f, 0.0f, 0.5f), chronon3d::sdk::Frame{0});
    REQUIRE(result);
    REQUIRE(result->pixels);
    REQUIRE(result->format == chronon3d::sdk::PixelFormat::RGBA8);
    REQUIRE(result->bytes_per_row == 5u * 4u);

    // Framebuffer rows are padded to four Colors; width 5 therefore has a
    // stride of 8 Colors. The second output row must not read that padding.
    const std::size_t pixel = result->bytes_per_row + 4u * 4u;
    CHECK(result->pixels[pixel + 0] == doctest::Approx(137).epsilon(1));
    CHECK(result->pixels[pixel + 1] == 0);
    CHECK(result->pixels[pixel + 2] == 0);
    CHECK(result->pixels[pixel + 3] == doctest::Approx(128).epsilon(1));
}
