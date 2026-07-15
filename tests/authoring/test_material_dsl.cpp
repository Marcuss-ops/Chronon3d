#include "authoring_dsl_test_support.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Material equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Material: default-constructed Material is disabled") {
    Material m;
    TextMaterial built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled == false);
}

TEST_CASE("Authoring/Material: material::premium() returns enabled premium preset") {
    Material m = material::premium();
    TextMaterial built = MaterialTestAccess::release(std::move(m));

    CHECK(built.enabled                 == true);
    CHECK(built.bevel_px                == doctest::Approx(1.5f));
    CHECK(built.bevel_highlight_opacity == doctest::Approx(0.45f));
    CHECK(built.bevel_shadow_opacity    == doctest::Approx(0.30f));
    CHECK(built.use_material_glow       == true);
    CHECK(built.glow_radius             == doctest::Approx(12.0f));
    CHECK(built.glow_intensity          == doctest::Approx(0.60f));
    CHECK(built.use_material_shadow     == true);
    CHECK(built.shadow_blur             == doctest::Approx(12.0f));
    CHECK(built.shadow_opacity          == doctest::Approx(0.45f));
    CHECK(built.emissive                == doctest::Approx(1.05f));
}

TEST_CASE("Authoring/Material: material::neon() enables strong glow") {
    Material m = material::neon();
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled           == true);
    CHECK(built.use_material_glow == true);
    CHECK(built.glow_radius       == doctest::Approx(20.0f));
    CHECK(built.glow_intensity    == doctest::Approx(1.0f));
    CHECK(built.emissive          == doctest::Approx(1.20f));
}

TEST_CASE("Authoring/Material: material::glass() enables semi-transparent gradient") {
    Material m = material::glass();
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled           == true);
    CHECK(built.bevel_px          == doctest::Approx(1.0f));
    CHECK(built.top_color.a       == doctest::Approx(0.85f).epsilon(0.001f));
    CHECK(built.bottom_color.a    == doctest::Approx(0.70f).epsilon(0.001f));
    CHECK(built.use_material_glow == true);
}

TEST_CASE("Authoring/Material: material::flat() does not enable glow or shadow") {
    Material m = material::flat();
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled             == true);
    CHECK(built.use_material_glow   == false);
    CHECK(built.use_material_shadow == false);
}

TEST_CASE("Authoring/Material: premium() + chained setters matches hand-built") {
    Material m = material::premium();
    m.bevel(1.5f)
     .glow(14.0f, 0.45f)
     .shadow(Vec2{0.0f, 8.0f}, 16.0f);

    auto built = MaterialTestAccess::release(std::move(m));

    TextMaterial manual;
    manual.enabled                    = true;
    manual.top_color                  = {1.0f, 1.0f, 1.0f, 1.0f};
    manual.bottom_color               = {0.85f, 0.87f, 0.95f, 1.0f};
    manual.bevel_px                   = 1.5f;
    manual.bevel_highlight_opacity    = 0.45f;
    manual.bevel_shadow_opacity       = 0.30f;
    manual.use_material_glow          = true;
    manual.glow_radius                = 14.0f;
    manual.glow_intensity             = 0.45f;
    manual.use_material_shadow        = true;
    manual.shadow_offset              = {0.0f, 8.0f};
    manual.shadow_blur                = 16.0f;
    manual.shadow_opacity             = 0.45f;
    manual.emissive                   = 1.05f;

    CHECK(built.enabled          == manual.enabled);
    CHECK(built.bevel_px         == doctest::Approx(manual.bevel_px));
    CHECK(built.glow_radius      == doctest::Approx(manual.glow_radius));
    CHECK(built.glow_intensity   == doctest::Approx(manual.glow_intensity));
    CHECK(built.shadow_offset.y  == doctest::Approx(manual.shadow_offset.y));
    CHECK(built.shadow_blur      == doctest::Approx(manual.shadow_blur));
    CHECK(built.shadow_opacity   == doctest::Approx(manual.shadow_opacity));
}

TEST_CASE("Authoring/Material: gradient() sets top + bottom + angle + enables") {
    Material m;
    m.gradient(Color{0.2f, 0.4f, 1.0f, 1.0f},
               Color{1.0f, 0.6f, 0.2f, 1.0f},
               45.0f);

    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled        == true);
    CHECK(built.top_color      == Color{0.2f, 0.4f, 1.0f, 1.0f});
    CHECK(built.bottom_color   == Color{1.0f, 0.6f, 0.2f, 1.0f});
    CHECK(built.gradient_angle == doctest::Approx(45.0f));
}

TEST_CASE("Authoring/Material: top_color / bottom_color single setters auto-enable") {
    Material m;
    m.top_color(Color{1.0f, 0.0f, 0.0f, 1.0f});
    m.bottom_color(Color{0.0f, 1.0f, 0.0f, 1.0f});
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled      == true);
    CHECK(built.top_color    == Color{1.0f, 0.0f, 0.0f, 1.0f});
    CHECK(built.bottom_color == Color{0.0f, 1.0f, 0.0f, 1.0f});
}

TEST_CASE("Authoring/Material: bevel(px) records pixel + default highlight/shadow") {
    Material m;
    m.bevel(2.5f);
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled                 == true);
    CHECK(built.bevel_px                == doctest::Approx(2.5f));
    CHECK(built.bevel_highlight_opacity == doctest::Approx(0.35f));
    CHECK(built.bevel_shadow_opacity    == doctest::Approx(0.25f));
}

TEST_CASE("Authoring/Material: bevel(px, highlight, shadow) sets all three") {
    Material m;
    m.bevel(3.0f, 0.8f, 0.4f);
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.bevel_px                == doctest::Approx(3.0f));
    CHECK(built.bevel_highlight_opacity == doctest::Approx(0.8f));
    CHECK(built.bevel_shadow_opacity    == doctest::Approx(0.4f));
}

TEST_CASE("Authoring/Material: shadow() 2/3/4-arg overloads accumulate fields") {
    Material m2;
    m2.shadow({1.0f, 2.0f}, 4.0f);
    auto b2 = MaterialTestAccess::release(std::move(m2));
    CHECK(b2.use_material_shadow == true);
    CHECK(b2.shadow_offset == doctest::Approx2D(Vec2{1.0f, 2.0f}));
    CHECK(b2.shadow_blur   == doctest::Approx(4.0f));

    Material m3;
    m3.shadow({0.0f, 3.0f}, 6.0f, 0.7f);
    auto b3 = MaterialTestAccess::release(std::move(m3));
    CHECK(b3.shadow_opacity == doctest::Approx(0.7f));

    Material m4;
    m4.shadow({0.0f, 5.0f}, 8.0f, 0.9f, Color{1.0f, 1.0f, 1.0f, 0.5f});
    auto b4 = MaterialTestAccess::release(std::move(m4));
    CHECK(b4.shadow_color == Color{1.0f, 1.0f, 1.0f, 0.5f});
}

TEST_CASE("Authoring/Material: glow + glow_color combine; no_glow disables") {
    Material m = material::premium();
    m.glow(8.0f, 0.5f).glow_color(Color{1.0f, 1.0f, 0.0f, 1.0f});
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.use_material_glow == true);
    CHECK(built.glow_radius       == doctest::Approx(8.0f));
    CHECK(built.glow_intensity    == doctest::Approx(0.5f));
    CHECK(built.glow_color        == Color{1.0f, 1.0f, 0.0f, 1.0f});

    Material off;
    off.glow(10.0f, 0.5f).no_glow();
    auto off_built = MaterialTestAccess::release(std::move(off));
    CHECK(off_built.use_material_glow == false);
    CHECK(off_built.glow_radius       == doctest::Approx(10.0f));
}

TEST_CASE("Authoring/Material: inner_shadow + no_inner_shadow toggle") {
    Material m;
    m.inner_shadow({2.0f, -1.0f}, 6.0f, Color{0.0f, 0.0f, 0.0f, 0.9f});
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.inner_shadow_enabled == true);
    CHECK(built.inner_shadow_offset == doctest::Approx2D(Vec2{2.0f, -1.0f}));
    CHECK(built.inner_shadow_blur   == doctest::Approx(6.0f));
    CHECK(built.inner_shadow_color  == Color{0.0f, 0.0f, 0.0f, 0.9f});
    CHECK(built.enabled             == true);

    Material off;
    off.inner_shadow({1, 1}, 1.0f, Color::black()).no_inner_shadow();
    CHECK(MaterialTestAccess::release(std::move(off)).inner_shadow_enabled == false);
}

TEST_CASE("Authoring/Material: top_highlight / bottom_shade set fractions") {
    Material m;
    m.top_highlight(0.5f, 0.20f)
     .bottom_shade(0.3f, 0.12f);
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.top_highlight_opacity  == doctest::Approx(0.5f));
    CHECK(built.top_highlight_fraction == doctest::Approx(0.20f));
    CHECK(built.bottom_shade_opacity   == doctest::Approx(0.3f));
    CHECK(built.bottom_shade_fraction  == doctest::Approx(0.12f));
    CHECK(built.enabled                == true);
}

TEST_CASE("Authoring/Material: emissive() can dim or boost brightness") {
    Material m1;
    m1.emissive(0.5f);
    CHECK(MaterialTestAccess::release(std::move(m1)).emissive == doctest::Approx(0.5f));

    Material m2;
    m2.emissive(1.5f);
    CHECK(MaterialTestAccess::release(std::move(m2)).emissive == doctest::Approx(1.5f));
}

TEST_CASE("Authoring/Material: enable() / disable() are explicit") {
    Material m;
    m.disable();
    CHECK(MaterialTestAccess::release(std::move(m)).enabled == false);

    Material m2;
    m2.enable();
    CHECK(MaterialTestAccess::release(std::move(m2)).enabled == true);
}

TEST_CASE("Authoring/Material: configure_core lambda mutates unmodeled field") {
    Material m = material::premium();
    m.configure_core([](TextMaterial& tm) {
        tm.bevel_highlight_color = Color{0.0f, 1.0f, 1.0f, 1.0f};
        tm.gradient_angle = 60.0f;
    });
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.bevel_highlight_color == Color{0.0f, 1.0f, 1.0f, 1.0f});
    CHECK(built.gradient_angle == doctest::Approx(60.0f));
}

TEST_CASE("Authoring/Material: copy is forbidden (move-only contract)") {
    CHECK(!std::is_copy_constructible_v<Material>);
    CHECK(!std::is_copy_assignable_v<Material>);
    CHECK(std::is_move_constructible_v<Material>);
    CHECK(std::is_move_assignable_v<Material>);
}

TEST_CASE("Authoring/Material: chain returns *this for stable identity") {
    Material m = material::premium();
    Material& r1 = m;
    Material& r2 = r1.bevel(2.0f);
    Material& r3 = r2.glow(8.0f, 0.5f);
    CHECK(&m  == &r1);
    CHECK(&r1 == &r2);
    CHECK(&r2 == &r3);
}
