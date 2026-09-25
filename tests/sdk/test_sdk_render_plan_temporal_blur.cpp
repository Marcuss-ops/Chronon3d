#include <doctest/doctest.h>

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kPlanPrefix = R"JSON({
  "schema":"chronon.render-plan.v3","version":3,
  "canvas":{"width":128,"height":128,"fps_num":24,"fps_den":1,"duration_frames":25},
  )JSON";
constexpr std::string_view kLayerAndOutput = R"JSON(
  "layers":[{"id":"moving","type":"color","size":[16,16],"position":[0,0],
    "color":[1,1,1,1],
    "animation":{"tracks":[{"property":"position_x","keyframes":[
      {"frame":0,"value":-32},{"frame":24,"value":32}]}]}}],
  "output":{"path":"unused.png"}
})JSON";

std::string plan_json(bool motion_blur) {
    std::string value{kPlanPrefix};
    if (motion_blur)
        value += R"JSON("temporal_motion_blur":{"shutter_angle":180,"samples":12},)JSON";
    value += kLayerAndOutput;
    return value;
}

std::vector<std::uint8_t> render_frame(chronon3d::sdk::RenderEngine& engine,
                                       std::string_view json) {
    const auto program = engine.compile_plan_json(json);
    REQUIRE(program.has_value());
    const auto result = engine.render_program(*program.value(), chronon3d::sdk::Frame{12},
                                              nullptr, 0);
    REQUIRE(result.has_value());
    REQUIRE(result->pixels != nullptr);
    const auto row_stride = result->bytes_per_row > 0
        ? result->bytes_per_row : static_cast<std::size_t>(result->width) * 4U;
    const auto byte_count = row_stride * static_cast<std::size_t>(result->height);
    return {result->pixels, result->pixels + byte_count};
}

} // namespace

TEST_CASE("Render Plan temporal shutter reaches SDK pixels and resets between plans") {
    chronon3d::sdk::RenderSettings settings{};
    settings.width = 128;
    settings.height = 128;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    chronon3d::sdk::RenderEngine engine{settings};

    const auto blurred = render_frame(engine, plan_json(true));
    const auto sharp = render_frame(engine, plan_json(false));
    REQUIRE(blurred.size() == sharp.size());

    std::size_t visible_channels = 0;
    std::size_t differing_channels = 0;
    for (std::size_t i = 0; i < blurred.size(); ++i) {
        visible_channels += blurred[i] != 0 || sharp[i] != 0 ? 1U : 0U;
        differing_channels += blurred[i] != sharp[i] ? 1U : 0U;
    }
    CHECK(visible_channels > 0);
    CHECK(differing_channels > 0);
}
