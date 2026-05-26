#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include "tests/helpers/render_fixtures.hpp"

#include <doctest/doctest.h>

using namespace chronon3d;

TEST_CASE("TextPerspectiveSweepDemo is registered") {
    CompositionRegistry registry;
    auto comp = registry.create("TextPerspectiveSweepDemo");
    REQUIRE(comp.name() == "TextPerspectiveSweepDemo");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.duration() == 150);
}

TEST_CASE("TextPerspectiveSweepDemo renders frame 0 (start)") {
    CompositionRegistry registry;
    auto comp = registry.create("TextPerspectiveSweepDemo");
    REQUIRE(comp.name() == "TextPerspectiveSweepDemo");

    auto fb = test::render_modular(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);

    // At frame 0: composition should have content (text panel with glow)
    Color c = fb->get_pixel(960, 540);
    CHECK(c.a > 0.0f);
}

TEST_CASE("TextPerspectiveSweepDemo renders frame 75 (mid-animation)") {
    CompositionRegistry registry;
    auto comp = registry.create("TextPerspectiveSweepDemo");

    auto fb = test::render_modular(comp, 75);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);

    // At frame 75 (50%): text should be visible, center has content
    Color c = fb->get_pixel(960, 540);
    CHECK(c.a > 0.0f);
}

TEST_CASE("TextPerspectiveSweepDemo renders frame 149 (end)") {
    CompositionRegistry registry;
    auto comp = registry.create("TextPerspectiveSweepDemo");

    auto fb = test::render_modular(comp, 149);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);

    // At last frame: text fully revealed with glow
    Color c = fb->get_pixel(960, 540);
    CHECK(c.a > 0.0f);
}

TEST_CASE("TextPerspectiveSweepDemo has correct FPS") {
    CompositionRegistry registry;
    auto comp = registry.create("TextPerspectiveSweepDemo");
    CHECK(comp.frame_rate().fps() == 30);
}
