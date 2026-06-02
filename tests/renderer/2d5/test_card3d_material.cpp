#include <doctest/doctest.h>
#include <chronon3d/scene/card3d_material.hpp>

using namespace chronon3d;

TEST_CASE("Card3DMaterial: defaults are disabled") {
    Card3DMaterial mat;
    CHECK_FALSE(mat.enabled);
    CHECK(mat.thickness_px == doctest::Approx(14.0f));
    CHECK(mat.edge_highlight_opacity == doctest::Approx(0.35f));
    CHECK(mat.rim_intensity == doctest::Approx(0.45f));
    CHECK(mat.rim_power == doctest::Approx(2.2f));
    CHECK(mat.corner_radius == doctest::Approx(28.0f));
    CHECK(mat.cast_shadow);
    CHECK(mat.receive_shadow);
}

TEST_CASE("Card3DMaterial: glass_card preset") {
    auto mat = Card3DMaterial::glass_card();
    CHECK(mat.enabled);
    CHECK(mat.thickness_px == doctest::Approx(10.0f));
    CHECK(mat.front_top_color.a < 1.0f); // translucent
    CHECK(mat.side_color.a < 1.0f);
    CHECK(mat.edge_highlight_opacity == doctest::Approx(0.50f));
    CHECK(mat.corner_radius == doctest::Approx(20.0f));
}

TEST_CASE("Card3DMaterial: dark_card preset") {
    auto mat = Card3DMaterial::dark_card();
    CHECK(mat.enabled);
    CHECK(mat.thickness_px == doctest::Approx(12.0f));
    // Dark card has very dark front colors
    CHECK(mat.front_top_color.r < 0.15f);
    CHECK(mat.front_bottom_color.r < 0.10f);
    CHECK(mat.rim_intensity == doctest::Approx(0.50f));
    CHECK(mat.cast_shadow);
}

TEST_CASE("Card3DMaterial: premium_card preset") {
    auto mat = Card3DMaterial::premium_card();
    CHECK(mat.enabled);
    CHECK(mat.thickness_px == doctest::Approx(14.0f));
    CHECK(mat.edge_highlight_color.r == doctest::Approx(0.50f));
    CHECK(mat.edge_highlight_color.g == doctest::Approx(0.65f));
    CHECK(mat.edge_highlight_color.b == doctest::Approx(1.0f));
    CHECK(mat.rim_intensity == doctest::Approx(0.55f));
    CHECK(mat.rim_power == doctest::Approx(2.2f));
    CHECK(mat.corner_radius == doctest::Approx(28.0f));
}

TEST_CASE("Card3DMaterial: flat preset") {
    auto mat = Card3DMaterial::flat();
    CHECK(mat.enabled);
    CHECK(mat.thickness_px == doctest::Approx(0.0f));
    CHECK(mat.edge_highlight_opacity == doctest::Approx(0.0f));
    CHECK(mat.rim_intensity == doctest::Approx(0.0f));
    CHECK_FALSE(mat.cast_shadow);
}

TEST_CASE("Card3DMaterial: preset colors are reasonable") {
    // All presets should have non-negative, <=1 color components
    auto glass = Card3DMaterial::glass_card();
    auto dark = Card3DMaterial::dark_card();
    auto premium = Card3DMaterial::premium_card();
    auto flat = Card3DMaterial::flat();
    for (auto* preset : {&glass, &dark, &premium, &flat}) {
        auto& m = *preset;
        CHECK(m.front_top_color.r >= 0.0f);
        CHECK(m.front_top_color.g >= 0.0f);
        CHECK(m.front_top_color.b >= 0.0f);
        CHECK(m.front_top_color.a >= 0.0f);
        CHECK(m.front_bottom_color.r >= 0.0f);
        CHECK(m.side_color.r >= 0.0f);
        CHECK(m.edge_highlight_color.r >= 0.0f);
    }
}
