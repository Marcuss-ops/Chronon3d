// ─── test_style_resolver.cpp — StyleResolver (ADR-029 forward-point (b)) ──
//
// Locks the canonical merge contract of
// `chronon3d::registry::VisualStyleResolver`:
//
//   preset defaults + job overrides = ResolvedVisualStyle
//
// covering defaults-only, override-wins, leave-base (absent), preset
// resolution through the registry, and unknown-id failure.

#include <doctest/doctest.h>

#include <chronon3d/registry/style_resolver.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::registry;
using render_plan::LayerStylePlan;
using render_plan::StrokeStyle;

namespace {

VisualStyle make_defaults() {
    VisualStyle s;
    s.font_family = "Poppins";
    s.font_asset = "assets/fonts/Poppins-Bold.ttf";
    s.font_weight = 700;
    s.font_size = 58.0f;
    s.fill = "#FFFFFF";
    s.stroke_color = "#000000";
    s.stroke_width = 2.0f;
    s.shadow_color = "#000000";
    s.shadow_opacity = 0.65f;
    s.shadow_blur = 16.0f;
    s.shadow_offset = std::array<float, 2>{0.0f, 6.0f};
    s.background_color = "#050509";
    s.background_opacity = 0.86f;
    s.radius = 12.0f;
    s.padding = std::array<float, 2>{24.0f, 14.0f};
    return s;
}

} // namespace

TEST_CASE("StyleResolver: defaults-only resolves every preset field") {
    const auto resolved = VisualStyleResolver{}.resolve(make_defaults(), nullptr);

    CHECK(resolved.font_family == "Poppins");
    CHECK(resolved.font_asset == "assets/fonts/Poppins-Bold.ttf");
    REQUIRE(resolved.font_weight.has_value());
    CHECK(*resolved.font_weight == 700);
    REQUIRE(resolved.font_size.has_value());
    CHECK(*resolved.font_size == doctest::Approx(58.0f));
    CHECK(resolved.fill == "#FFFFFF");

    CHECK(resolved.stroke_enabled);
    CHECK(resolved.stroke_color == "#000000");
    REQUIRE(resolved.stroke_width.has_value());
    CHECK(*resolved.stroke_width == doctest::Approx(2.0f));

    CHECK(resolved.shadow_enabled);
    CHECK(resolved.shadow_color == "#000000");
    REQUIRE(resolved.shadow_opacity.has_value());
    CHECK(*resolved.shadow_opacity == doctest::Approx(0.65f));
    REQUIRE(resolved.shadow_blur.has_value());
    CHECK(*resolved.shadow_blur == doctest::Approx(16.0f));
    REQUIRE(resolved.shadow_offset.has_value());
    CHECK((*resolved.shadow_offset)[1] == doctest::Approx(6.0f));

    CHECK(resolved.background_enabled);
    CHECK(resolved.background_color == "#050509");
    REQUIRE(resolved.background_opacity.has_value());
    CHECK(*resolved.background_opacity == doctest::Approx(0.86f));
    REQUIRE(resolved.radius.has_value());
    CHECK(*resolved.radius == doctest::Approx(12.0f));
    REQUIRE(resolved.padding.has_value());
    CHECK((*resolved.padding)[0] == doctest::Approx(24.0f));
}

TEST_CASE("StyleResolver: job override wins over preset default") {
    LayerStylePlan o;
    o.font_size = 40.0f;
    o.fill = "#FFCC00";
    o.stroke = StrokeStyle{};
    o.stroke->color = "#FFFFFF";
    o.stroke->width = 3.0f;

    const auto resolved = VisualStyleResolver{}.resolve(make_defaults(), &o);

    // Overridden fields win.
    REQUIRE(resolved.font_size.has_value());
    CHECK(*resolved.font_size == doctest::Approx(40.0f));
    CHECK(resolved.fill == "#FFCC00");
    CHECK(resolved.stroke_color == "#FFFFFF");
    REQUIRE(resolved.stroke_width.has_value());
    CHECK(*resolved.stroke_width == doctest::Approx(3.0f));

    // Non-overridden fields keep the preset default.
    CHECK(resolved.font_family == "Poppins");
    REQUIRE(resolved.font_weight.has_value());
    CHECK(*resolved.font_weight == 700);
    CHECK(resolved.shadow_enabled);
    CHECK(resolved.background_enabled);
}

TEST_CASE("StyleResolver: empty preset leaves every field base") {
    const VisualStyle empty;
    const auto resolved = VisualStyleResolver{}.resolve(empty, nullptr);

    CHECK(resolved.font_family.empty());
    CHECK_FALSE(resolved.font_weight.has_value());
    CHECK_FALSE(resolved.font_size.has_value());
    CHECK(resolved.fill.empty());
    CHECK_FALSE(resolved.stroke_enabled);
    CHECK_FALSE(resolved.shadow_enabled);
    CHECK_FALSE(resolved.background_enabled);
}

TEST_CASE("StyleResolver: resolve_preset resolves the full lower_third_safe recipe") {
    const auto& registry = builtin_visual_preset_registry();
    const auto resolved = VisualStyleResolver{}.resolve_preset(
        registry, "lower_third_safe", nullptr);

    // Canonical preset font family (registry-owned, see ADR-029): Poppins
    // is an asset in the golden jobs, never a silent system fallback.
    CHECK(resolved.font_family == "Poppins");
    CHECK(resolved.stroke_enabled);
    CHECK(resolved.shadow_enabled);
    CHECK(resolved.background_enabled);
    REQUIRE(resolved.font_size.has_value());
    CHECK(*resolved.font_size == doctest::Approx(58.0f));
}

TEST_CASE("StyleResolver: resolve_preset merges override into preset defaults") {
    const auto& registry = builtin_visual_preset_registry();
    LayerStylePlan o;
    o.font_size = 50.0f;

    const auto resolved = VisualStyleResolver{}.resolve_preset(
        registry, "caption_card", &o);

    REQUIRE(resolved.font_size.has_value());
    CHECK(*resolved.font_size == doctest::Approx(50.0f));
    CHECK(resolved.font_family == "Poppins");  // preset default carried through
    REQUIRE(resolved.font_weight.has_value());
    CHECK(*resolved.font_weight == 700);
    CHECK(resolved.background_enabled);            // caption_card has a card
}

TEST_CASE("StyleResolver: resolve_preset throws on unknown id") {
    const auto& registry = builtin_visual_preset_registry();
    CHECK_THROWS_AS(
        VisualStyleResolver{}.resolve_preset(registry, "nope", nullptr),
        std::runtime_error);
}
