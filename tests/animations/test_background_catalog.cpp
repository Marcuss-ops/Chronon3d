#include <chronon3d/api/backgrounds.hpp>
#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::api;

TEST_CASE("Builtin background catalog exposes grid_clean preset") {
    const auto& catalog = builtin_background_catalog();

    REQUIRE(catalog.presets.size() == 1);

    const auto* clean = find_builtin_background("grid_clean");
    REQUIRE(clean != nullptr);
    CHECK(clean->name == "Grid Clean");
    CHECK(clean->loopable);

    const std::string json = builtin_background_catalog_json();
    const auto parsed = nlohmann::json::parse(json);
    REQUIRE(parsed.is_array());
    CHECK(parsed.size() == catalog.presets.size());
}

TEST_CASE("Builtin background compositions are registered") {
    chronon3d::CompositionRegistry registry;
    CHECK(registry.create("GridCleanBackground").name() == "GridCleanBackground");
}

TEST_CASE("Analytical verification of GridCleanBackground render") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    chronon3d::CompositionRegistry registry;
    auto comp = registry.create("GridCleanBackground");
    REQUIRE(comp.name() == "GridCleanBackground");
    
    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);

    // Verify background color at a non-grid pixel (e.g. 70px offset from center 960, 540)
    // The grid spacing is 140px, so (960 + 70, 540 + 70) = (1030, 610) is exactly in the center of a grid cell.
    Color bg_pixel = fb->get_pixel(1030, 610);
    Color expected_bg = Color{0.008f, 0.010f, 0.022f, 1.0f}.to_linear();
    CHECK(std::abs(bg_pixel.r - expected_bg.r) < 1e-4f);
    CHECK(std::abs(bg_pixel.g - expected_bg.g) < 1e-4f);
    CHECK(std::abs(bg_pixel.b - expected_bg.b) < 1e-4f);
    CHECK(std::abs(bg_pixel.a - expected_bg.a) < 1e-4f);

    // Verify grid line presence at (960, 540), which is the center intersection point.
    // It should have some blended blue accent color (accent = Color{0.25f, 0.52f, 1.0f, 0.05f}).
    Color grid_pixel = fb->get_pixel(960, 540);
    CHECK(grid_pixel.b > expected_bg.b);
}
