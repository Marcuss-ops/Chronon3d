#include <doctest/doctest.h>
#include <chronon3d/presets/text/text_style_presets.hpp>
using namespace chronon3d;


TEST_CASE("Text presets: premium stack exposes richer text styles") {
    SUBCASE("Premium hero title has gradient, stroke and material") {
        const TextStyle style = presets::text::premium_hero_title();
        CHECK(style.paint.fill_style.has_value());
        CHECK(style.paint.stroke_enabled == true);
        CHECK(style.paint.stroke_width > 0.0f);
        CHECK(style.material.enabled == true);
        CHECK(style.shadows.size() >= 2);
    }

    SUBCASE("Premium CTA keeps glassy centered label styling") {
        const TextStyle style = presets::text::premium_cta();
        CHECK(style.align == TextAlign::Center);
        CHECK(style.vertical_align == VerticalAlign::Middle);
        CHECK(style.paint.stroke_enabled == true);
        CHECK(style.material.enabled == true);
        CHECK(style.shadows.size() >= 1);
    }
}

TEST_CASE("Text presets: shadow/stroke quick catalog exposes graduated weights") {
    using chronon3d::presets::text::ShadowStrokePreset;
    using chronon3d::presets::text::shadow_stroke;

    const auto crisp = shadow_stroke(ShadowStrokePreset::Crisp);
    const auto soft = shadow_stroke(ShadowStrokePreset::Soft);
    const auto heavy = shadow_stroke(ShadowStrokePreset::Heavy);
    const auto neon = shadow_stroke(ShadowStrokePreset::Neon);

    CHECK(crisp.paint.stroke_enabled);
    CHECK(soft.paint.stroke_enabled);
    CHECK(heavy.paint.stroke_enabled);
    CHECK(neon.paint.stroke_enabled);
    CHECK(crisp.shadows.size() == 1);
    CHECK(soft.shadows.size() == 1);
    CHECK(heavy.shadows.size() == 1);
    CHECK(neon.shadows.size() == 2);
    CHECK(crisp.shadows.front().blur < soft.shadows.front().blur);
    CHECK(soft.shadows.front().blur < heavy.shadows.front().blur);
    CHECK(crisp.paint.stroke_width < soft.paint.stroke_width);
    CHECK(soft.paint.stroke_width < heavy.paint.stroke_width);

    const auto recommended = chronon3d::presets::text::typewriter_shadow_stroke();
    CHECK(recommended.paint.stroke_enabled);
    CHECK(recommended.shadows.size() == 1);
    CHECK(recommended.shadows.front().blur == doctest::Approx(8.0f));
}
