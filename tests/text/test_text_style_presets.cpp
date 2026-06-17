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
        CHECK(style.material.use_material_glow == true);
        CHECK(style.shadows.size() >= 2);
    }

    SUBCASE("Premium CTA keeps glassy centered label styling") {
        const TextStyle style = presets::text::premium_cta();
        CHECK(style.align == TextAlign::Center);
        CHECK(style.vertical_align == VerticalAlign::Middle);
        CHECK(style.paint.stroke_enabled == true);
        CHECK(style.material.enabled == true);
        CHECK(style.material.use_material_glow == true);
    }
}
