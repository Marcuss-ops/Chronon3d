#include <doctest/doctest.h>

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>

#include <cstddef>
#include <string_view>

namespace {

// A 5x2 full-canvas color layer. The 0.25 linear red and 0.5 alpha exercise
// the straight-sRGB conversion and the padded framebuffer row (width 5 pads
// to 8 Colors), which is the contract under test.
constexpr std::string_view kPlan = R"JSON({
  "schema":"chronon.render-plan.v3","version":3,
  "canvas":{"width":5,"height":2,"fps_num":30,"fps_den":1,"duration_frames":1},
  "layers":[{"id":"pixel","type":"color","size":[5,2],"position":[0,0],
    "color":[0.25,0,0,0.5],"start_frame":0,"duration_frames":1}],
  "output":{"path":"unused.png"}
})JSON";

}  // namespace

TEST_CASE("SDK RGBA8 conversion walks logical rows and emits straight sRGB pixels") {
    chronon3d::sdk::RenderSettings settings{};
    settings.width = 5;
    settings.height = 2;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    chronon3d::sdk::RenderEngine engine{settings};

    const auto compiled = engine.compile_plan_json(kPlan);
    REQUIRE(compiled.has_value());

    const auto result = engine.render_program(*compiled.value(),
                                              chronon3d::sdk::Frame{0});
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
