#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <tests/helpers/test_utils.hpp>
#include <nlohmann/json.hpp>

#include <bit>
#include <cstdint>

using namespace chronon3d;

TEST_CASE("monotone chart CPU render includes area, data line, grid, and axes") {
    assets::AssetResolver resolver;
    const auto decoded = render_plan::decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":128,"height":96,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[
        {"id":"background","type":"color","color":[1,1,1,1]},
        {"id":"chart","type":"shape","size":[128,96],"shape":{
          "type":"monotone_chart","data_points":[[0,2],[5,8],[10,4]],
          "x_domain":[0,10],"y_domain":[0,10],"x_ticks":5,"y_ticks":5,
          "area_color":"#6699CC","line_color":"#102060",
          "grid_color":"#B0B0B0","axis_color":"#101010"}}
      ],"output":{"path":"out.png"}})JSON"));
    const std::string decode_error = decoded ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded, decode_error);
    const auto prepared = render_plan::compile_render_plan(*decoded, resolver);
    const std::string compile_error = prepared ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared, compile_error);
    auto renderer = test::make_renderer();
    const auto first = renderer.render(*prepared->composition, 0);
    const auto second = renderer.render(*prepared->composition, 0);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->get_pixel(74, 51).b > first->get_pixel(74, 51).r);
    CHECK(first->get_pixel(64, 64).r < 0.6f);
    CHECK(first->get_pixel(76, 32).r < 0.6f);
    CHECK(first->get_pixel(57, 44).r < 0.95f);
    const auto hash = [](const Framebuffer& framebuffer) {
        std::uint64_t value = 1469598103934665603ULL;
        for (int y = 0; y < framebuffer.height(); ++y)
        for (int x = 0; x < framebuffer.width(); ++x) {
            const auto pixel = framebuffer.get_pixel(x, y);
            for (float channel : {pixel.r, pixel.g, pixel.b, pixel.a}) {
                value ^= std::bit_cast<std::uint32_t>(channel);
                value *= 1099511628211ULL;
            }
        }
        return value;
    };
    CHECK(hash(*first) == hash(*second));
}
