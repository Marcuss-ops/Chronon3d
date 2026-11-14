#include <doctest/doctest.h>

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
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

// A 2.5D camera plan with a full-canvas 2D background and a projected text
// layer. The background has no enable_3d flag: it must stay on the authored
// submission order and must not acquire a camera-space depth from its
// full-canvas centre. Otherwise a negative camera yaw moves the background in
// front of the text and the title disappears.
constexpr std::string_view kYawPlanPrefix = R"JSON({
  "schema":"chronon.render-plan.v3","version":3,
  "canvas":{"width":960,"height":540,"fps_num":24,"fps_den":1,"duration_frames":1},
  "camera":{"type":"perspective","position":[0,0,-1000],"rotation_deg":[0,)JSON";
constexpr std::string_view kYawPlanSuffix = R"JSON(,0],"fov_deg":42,"near":0.1,"far":10000},
  "layers":[
    {"id":"bg","type":"color","size":[960,540],"position":[480,270],
      "color":[1,0,0,1],"start_frame":0,"duration_frames":1},
    {"id":"title","type":"text","text":"MOTION","size":[860,300],
      "position":[480,270],"enable_3d":true,
      "style":{"font":"assets/fonts/Inter-Bold.ttf","font_size":148,"fill":"#00FF00"},
      "start_frame":0,"duration_frames":1}
  ],
  "output":{"path":"unused.png"}
})JSON";

std::string yaw_plan(int yaw) {
    std::string plan{kYawPlanPrefix};
    plan += std::to_string(yaw);
    plan += kYawPlanSuffix;
    return plan;
}

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

TEST_CASE("SDK RenderPlan keeps 3D text visible over a 2D background at negative camera yaw") {
    chronon3d::sdk::RenderSettings settings{};
    settings.width = 960;
    settings.height = 540;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    chronon3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(
        std::filesystem::absolute(__FILE__).parent_path().parent_path().parent_path());

    for (const int yaw : {-2, 2}) {
        const auto compiled = engine.compile_plan_json(yaw_plan(yaw));
        REQUIRE(compiled.has_value());
        const auto rendered = engine.render_program(*compiled.value(),
                                                    chronon3d::sdk::Frame{0});
        REQUIRE(rendered.has_value());
        REQUIRE(rendered->format == chronon3d::sdk::PixelFormat::RGBA8);
        REQUIRE(rendered->pixels != nullptr);

        const auto stride = rendered->bytes_per_row > 0
            ? rendered->bytes_per_row
            : static_cast<std::size_t>(rendered->width) * 4U;
        std::size_t green_pixels = 0;
        for (int y = 0; y < rendered->height; ++y) {
            for (int x = 0; x < rendered->width; ++x) {
                const auto* px = rendered->pixels + y * stride + x * 4U;
                if (px[0] < 128 && px[1] > 128 && px[2] < 128) ++green_pixels;
            }
        }
        INFO("yaw=", yaw, " green_pixels=", green_pixels);
        CHECK(green_pixels > 500);
    }
}
