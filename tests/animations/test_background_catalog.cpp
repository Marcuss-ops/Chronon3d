#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>

#include <doctest/doctest.h>
#include <cmath>
using namespace chronon3d;


// Register built-in compositions once before any test case.
static bool _bg_registered = (chronon3d::register_builtin_compositions(), true);

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
    CHECK(std::abs(bg_pixel.r - expected_bg.r) < 2e-3f);
    CHECK(std::abs(bg_pixel.g - expected_bg.g) < 2e-3f);
    CHECK(std::abs(bg_pixel.b - expected_bg.b) < 2e-3f);
    CHECK(std::abs(bg_pixel.a - expected_bg.a) < 1e-4f);

    // Verify grid line presence at (960, 540), which is the center intersection point.
    // It should have some blended blue accent color (accent = Color{0.25f, 0.52f, 1.0f, 0.05f}).
    Color grid_pixel = fb->get_pixel(960, 540);
    CHECK(grid_pixel.b > expected_bg.b);
    CHECK(renderer.counters()->clear_skipped_calls.load() > 0);
}
