#include "authoring_dsl_test_support.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Selector equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Selector: shape + unit + exclude_spaces round-trip") {
    Selector s{TextSelectorUnit::Grapheme};
    s.shape(TextSelectorShape::RampUp)
     .exclude_spaces();

    GlyphSelectorSpec built = AnimatorTestAccess::release(std::move(s));

    CHECK(built.unit            == TextSelectorUnit::Grapheme);
    CHECK(built.shape           == TextSelectorShape::RampUp);
    CHECK(built.exclude_spaces  == true);
    CHECK(built.order           == TextSelectorOrder::Forward);
    CHECK(built.combine         == SelectorCombineMode::Replace);
}

TEST_CASE("Authoring/Selector: smooth() sets TextSelectorShape::Smooth") {
    Selector s = selector(TextSelectorUnit::Word);
    s.smooth().exclude_spaces(false);

    auto built = AnimatorTestAccess::release(std::move(s));
    CHECK(built.unit           == TextSelectorUnit::Word);
    CHECK(built.shape          == TextSelectorShape::Smooth);
    CHECK(built.exclude_spaces == false);
}

TEST_CASE("Authoring/Selector: multiple .start() calls accumulate keyframes") {
    Selector s = selector(TextSelectorUnit::Glyph);
    s.start(Frame{0},    0.0f)
     .start(Frame{24},  50.0f)
     .start(Frame{60}, 100.0f, Easing::OutCubic);

    auto built = AnimatorTestAccess::release(std::move(s));
    CHECK(built.start.evaluate(0)  == doctest::Approx(0.0f));
    CHECK(built.start.evaluate(24) == doctest::Approx(50.0f));
    CHECK(built.start.evaluate(60) == doctest::Approx(100.0f));
}

TEST_CASE("Authoring/Selector: end / offset / amount setters apply") {
    Selector s = selector(TextSelectorUnit::Line);
    s.end   (Frame{60},   80.0f, Easing::InOutCubic)
     .offset(Frame{0},   -10.0f)
     .amount(Frame{30},   75.0f)
     .ease_low (15.0f)
     .ease_high(85.0f);

    auto built = AnimatorTestAccess::release(std::move(s));
    CHECK(built.unit           == TextSelectorUnit::Line);
    CHECK(built.ease_low       == doctest::Approx(15.0f));
    CHECK(built.ease_high      == doctest::Approx(85.0f));
    CHECK(built.exclude_spaces == true);

    const SelectorWeight w_end = chronon3d::evaluate_selector(
        built, {}, 0, "", chronon3d::SampleTime{60}, nullptr);
    CHECK(w_end <= 0.81f);
}

TEST_CASE("Authoring/Selector: chain returns *this for single Animator identity") {
    Selector s = selector(TextSelectorUnit::Glyph);
    Selector& ref1 = s;
    Selector& ref2 = ref1.start(0, 0.0f);
    Selector& ref3 = ref2.smooth();
    CHECK(&s    == &ref1);
    CHECK(&ref1 == &ref2);
    CHECK(&ref2 == &ref3);
}

// ═══════════════════════════════════════════════════════════════════════════
// Animator equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Animator: fluent hero-reveal pattern") {
    Animator a = animator("hero-reveal");

    Selector sel = selector(TextSelectorUnit::Grapheme);
    sel.smooth()
       .exclude_spaces()
       .start(Frame{0},  0.0f)
       .start(Frame{24}, 100.0f, Easing::OutCubic);

    a.select(std::move(sel))
     .position(Vec3{0.0f, 46.0f, 0.0f})
     .scale(0.94f)
     .opacity(0.0f)
     .blur(12.0f)
     .tracking(10.0f);

    auto built = AnimatorTestAccess::release(std::move(a));

    CHECK(built.id             == "hero-reveal");
    CHECK(built.enabled        == true);
    CHECK(built.transform_mode == TextPropertyBlendMode::Add);
    CHECK(built.color_mode     == TextPropertyBlendMode::Replace);

    REQUIRE(built.selectors.size() == 1);
    const auto& s0 = built.selectors[0];
    CHECK(s0.unit           == TextSelectorUnit::Grapheme);
    CHECK(s0.shape          == TextSelectorShape::Smooth);
    CHECK(s0.exclude_spaces == true);

    REQUIRE(built.properties.size() == 5);
    CHECK(find_property(built, typeid(chronon3d::PositionProperty),
        [](const void* p) {
            return static_cast<const chronon3d::PositionProperty*>(p)->value.evaluate(0)
                == Vec3{0.0f, 46.0f, 0.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::ScaleProperty),
        [](const void* p) {
            auto* sp = static_cast<const chronon3d::ScaleProperty*>(p);
            return sp->value.evaluate(0).x == doctest::Approx(0.94f)
                && sp->value.evaluate(0).y == doctest::Approx(0.94f)
                && sp->value.evaluate(0).z == doctest::Approx(1.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::OpacityProperty),
        [](const void* p) {
            return static_cast<const chronon3d::OpacityProperty*>(p)->value.evaluate(0)
                == doctest::Approx(0.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::BlurProperty),
        [](const void* p) {
            return static_cast<const chronon3d::BlurProperty*>(p)->radius.evaluate(0)
                == doctest::Approx(12.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::TrackingProperty),
        [](const void* p) {
            return static_cast<const chronon3d::TrackingProperty*>(p)->pixels.evaluate(0)
                == doctest::Approx(10.0f);
        }));
}

TEST_CASE("Authoring/Animator: empty builder carries Spec defaults") {
    Animator a = animator("defaults");
    auto built = AnimatorTestAccess::release(std::move(a));

    CHECK(built.id             == "defaults");
    CHECK(built.enabled        == true);
    CHECK(built.transform_mode == TextPropertyBlendMode::Add);
    CHECK(built.color_mode     == TextPropertyBlendMode::Replace);
    CHECK(built.selectors.empty());
    CHECK(built.properties.empty());
}

TEST_CASE("Authoring/Animator: Scale overload picks Vec3 vs uniform") {
    Animator u = animator("uniform");
    u.scale(0.5f);
    auto u_built = AnimatorTestAccess::release(std::move(u));
    REQUIRE(u_built.properties.size() == 1);
    auto* sp = std::get_if<chronon3d::ScaleProperty>(&u_built.properties[0]);
    REQUIRE(sp != nullptr);
    CHECK(sp->value.evaluate(0) == doctest::Approx3D(Vec3{0.5f, 0.5f, 1.0f}));

    Animator v = animator("vec3");
    v.scale(Vec3{0.5f, 0.25f, 2.0f});
    auto v_built = AnimatorTestAccess::release(std::move(v));
    REQUIRE(v_built.properties.size() == 1);
    auto* sp2 = std::get_if<chronon3d::ScaleProperty>(&v_built.properties[0]);
    REQUIRE(sp2 != nullptr);
    CHECK(sp2->value.evaluate(0) == doctest::Approx3D(Vec3{0.5f, 0.25f, 2.0f}));
}

TEST_CASE("Authoring/Animator: rotation / anchor / colours apply") {
    Animator a = animator("visuals");
    a.rotation(Vec3{0.0f, 15.0f, 0.0f})
     .anchor(Vec3{10.0f, 0.0f, 0.0f})
     .fill_color(Color{1.0f, 0.5f, 0.25f, 1.0f})
     .stroke_color(Color{0.0f, 0.0f, 1.0f, 1.0f})
     .stroke_width(2.5f);

    auto built = AnimatorTestAccess::release(std::move(a));
    REQUIRE(built.properties.size() == 5);
    CHECK(find_property(built, typeid(chronon3d::RotationProperty),
        [](const void* p) {
            return static_cast<const chronon3d::RotationProperty*>(p)->degrees
                == doctest::Approx3D(Vec3{0.0f, 15.0f, 0.0f});
        }));
    CHECK(find_property(built, typeid(chronon3d::AnchorProperty),
        [](const void* p) {
            return static_cast<const chronon3d::AnchorProperty*>(p)->value
                == doctest::Approx3D(Vec3{10.0f, 0.0f, 0.0f});
        }));
    CHECK(find_property(built, typeid(chronon3d::FillColorProperty),
        [](const void* p) {
            return static_cast<const chronon3d::FillColorProperty*>(p)->color
                == Color{1.0f, 0.5f, 0.25f, 1.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::StrokeColorProperty),
        [](const void* p) {
            return static_cast<const chronon3d::StrokeColorProperty*>(p)->color
                == Color{0.0f, 0.0f, 1.0f, 1.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::StrokeWidthProperty),
        [](const void* p) {
            return static_cast<const chronon3d::StrokeWidthProperty*>(p)->width
                == doctest::Approx(2.5f);
        }));
}

TEST_CASE("Authoring/Animator: multiple selectors accumulate in order") {
    Animator a = animator("stacked");

    Selector s1 = selector(TextSelectorUnit::Word);
    s1.start(Frame{0}, 50.0f);

    Selector s2 = selector(TextSelectorUnit::Line);
    s2.ease_high(80.0f);

    a.select(std::move(s1))
     .select(std::move(s2))
     .opacity(1.0f);

    auto built = AnimatorTestAccess::release(std::move(a));
    REQUIRE(built.selectors.size() == 2);
    CHECK(built.selectors[0].unit == TextSelectorUnit::Word);
    CHECK(built.selectors[1].unit == TextSelectorUnit::Line);
    CHECK(built.selectors[1].ease_high == doctest::Approx(80.0f));
    REQUIRE(built.properties.size() == 1);
}

TEST_CASE("Authoring/Animator: chain returns *this for stable identity") {
    Animator a = animator("chain");
    Animator& r1 = a;
    Animator& r2 = r1.position(Vec3{1.0f, 2.0f, 3.0f});
    Animator& r3 = r2.opacity(0.5f);
    CHECK(&a  == &r1);
    CHECK(&r1 == &r2);
    CHECK(&r2 == &r3);
}

TEST_CASE("Authoring/Animator: copy is forbidden (move-only contract)") {
    Animator a = animator("m");
    CHECK(!std::is_copy_constructible_v<Animator>);
    CHECK(!std::is_copy_assignable_v<Animator>);
    CHECK(std::is_move_constructible_v<Animator>);
    CHECK(std::is_move_assignable_v<Animator>);
    (void)a.opacity(1.0f);
}

TEST_CASE("Authoring/Selector: copy is forbidden (move-only contract)") {
    Selector s = selector(TextSelectorUnit::Glyph);
    CHECK(!std::is_copy_constructible_v<Selector>);
    CHECK(!std::is_copy_assignable_v<Selector>);
    CHECK(std::is_move_constructible_v<Selector>);
    CHECK(std::is_move_assignable_v<Selector>);
    (void)s.smooth();
}
