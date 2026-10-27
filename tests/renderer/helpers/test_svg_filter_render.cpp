#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <tests/helpers/test_utils.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <filesystem>
#include <fstream>
#include <string>

using namespace chronon3d;

TEST_CASE("SVG software filter chain produces deterministic pixels") {
    const auto root = std::filesystem::temp_directory_path() /
        ("chronon3d_svg_filter_render_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    {
        std::ofstream svg(root / "filter.svg");
        svg << R"SVG(<svg width="64" height="64" filter="url(#fx)">
<defs><filter id="fx"><feGaussianBlur stdDeviation="1" result="blur"/>
<feDropShadow in="blur" dx="2" dy="1" stdDeviation="1" flood-color="#ff0000" flood-opacity="0.5" result="shadow"/>
<feColorMatrix in="shadow" type="matrix" values="1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1 0" result="color"/>
<feBlend in="color" in2="BackgroundImage" mode="multiply"/></filter></defs>
<path d="M8 8 H56 V56 H8 Z" fill="#00ff00"/></svg>)SVG";
    }
    assets::AssetResolver resolver;
    resolver.mount(root);
    const auto decoded = render_plan::decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"bg","type":"color","color":[1,1,1,1]},
        {"id":"svg","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"filter.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = render_plan::compile_render_plan(*decoded, resolver);
    const std::string preparation_error = prepared.has_value()
        ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), preparation_error);
    auto renderer = test::make_renderer();
    const auto first = renderer.render(*prepared->composition, 0);
    const auto second = renderer.render(*prepared->composition, 0);
    REQUIRE(first);
    REQUIRE(second);
    const auto center_a = first->get_pixel(32, 32);
    const auto center_b = second->get_pixel(32, 32);
    CHECK(center_a.a > 0.9f);
    CHECK(center_a.r == doctest::Approx(center_b.r));
    CHECK(center_a.g == doctest::Approx(center_b.g));
    CHECK(center_a.b == doctest::Approx(center_b.b));
    CHECK(center_a.a == doctest::Approx(center_b.a));
    CHECK(first->get_pixel(59, 32).a > 0.0f);
    const auto hash_pixels = [](const Framebuffer& framebuffer) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (int y = 0; y < framebuffer.height(); ++y)
            for (int x = 0; x < framebuffer.width(); ++x) {
                const auto pixel = framebuffer.get_pixel(x, y);
                for (const float channel : {pixel.r, pixel.g, pixel.b, pixel.a}) {
                    hash ^= std::bit_cast<std::uint32_t>(channel);
                    hash *= 1099511628211ULL;
                }
            }
        return hash;
    };
    CHECK(hash_pixels(*first) == hash_pixels(*second));
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
