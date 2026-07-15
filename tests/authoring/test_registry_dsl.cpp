#include "authoring_dsl_test_support.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// StyleRegistry equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/StyleRegistry: empty registry resolves nullopt") {
    StyleRegistry reg;
    CHECK(reg.count() == 0);
    CHECK(reg.has("any.id") == false);
    CHECK(reg.resolve("any.id") == std::nullopt);
    CHECK(reg.ids().empty());
}

TEST_CASE("Authoring/StyleRegistry: register_style and resolve return value") {
    StyleRegistry reg;
    TextStyle hero;
    hero.font_path = "assets/fonts/Inter-Bold.ttf";
    hero.size = 96.0f;
    hero.color = Color{1.0f, 0.86f, 0.2f, 1.0f};
    hero.anchor = TextAnchor::Center;
    hero.align = TextAlign::Center;
    hero.line_height = 1.0f;
    reg.register_style("youtube.hero.premium", hero);

    CHECK(reg.count() == 1);
    CHECK(reg.has("youtube.hero.premium"));
    auto resolved = reg.resolve("youtube.hero.premium");
    REQUIRE(resolved.has_value());
    CHECK(resolved->font_path == "assets/fonts/Inter-Bold.ttf");
    CHECK(resolved->size == doctest::Approx(96.0f));
    CHECK(resolved->color == Color{1.0f, 0.86f, 0.2f, 1.0f});
}

TEST_CASE("Authoring/StyleRegistry: register_factory invokes factory per resolve") {
    StyleRegistry reg;
    int call_count = 0;
    reg.register_factory("parametric.style", [&call_count]() -> TextStyle {
        ++call_count;
        TextStyle s;
        s.size = 48.0f * static_cast<f32>(call_count);
        return s;
    });

    REQUIRE(call_count == 0);
    auto r1 = reg.resolve("parametric.style");
    REQUIRE(r1.has_value());
    CHECK(r1->size == doctest::Approx(48.0f));
    CHECK(call_count == 1);
    auto r2 = reg.resolve("parametric.style");
    REQUIRE(r2.has_value());
    CHECK(r2->size == doctest::Approx(96.0f));
    CHECK(call_count == 2);
    auto r3 = reg.resolve("parametric.style");
    REQUIRE(r3.has_value());
    CHECK(r3->size == doctest::Approx(144.0f));
    CHECK(call_count == 3);
}

TEST_CASE("Authoring/StyleRegistry: register_style overrides previous") {
    StyleRegistry reg;
    TextStyle a;
    a.size = 48.0f;
    reg.register_style("hero", a);
    TextStyle b;
    b.size = 96.0f;
    reg.register_style("hero", b);

    CHECK(reg.count() == 1);
    auto resolved = reg.resolve("hero");
    REQUIRE(resolved.has_value());
    CHECK(resolved->size == doctest::Approx(96.0f));
}

TEST_CASE("Authoring/StyleRegistry: empty-id and null-factory are no-ops") {
    StyleRegistry reg;
    TextStyle hero;
    hero.size = 48.0f;
    reg.register_style({}, hero);
    CHECK(reg.count() == 0);
    reg.register_factory("valid.id", nullptr);
    CHECK(reg.count() == 0);
    reg.register_style("", hero);
    CHECK(reg.count() == 0);
    reg.register_style("ok", hero);
    CHECK(reg.count() == 1);
}

TEST_CASE("Authoring/StyleRegistry: unregister returns bool and removes entry") {
    StyleRegistry reg;
    TextStyle s;
    s.size = 48.0f;
    reg.register_style("k1", s);
    reg.register_style("k2", s);
    CHECK(reg.count() == 2);
    CHECK(reg.unregister("k1") == true);
    CHECK(reg.unregister("k1") == false);
    CHECK(reg.unregister("absent") == false);
    CHECK(reg.count() == 1);
    CHECK(reg.has("k1") == false);
    CHECK(reg.has("k2") == true);
}

TEST_CASE("Authoring/StyleRegistry: ids() returns sorted list") {
    StyleRegistry reg;
    TextStyle s;
    reg.register_style("z.last", s);
    reg.register_style("a.first", s);
    reg.register_style("m.middle", s);
    auto ids = reg.ids();
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == "a.first");
    CHECK(ids[1] == "m.middle");
    CHECK(ids[2] == "z.last");
}

TEST_CASE("Authoring/StyleRegistry: copy / move forbidden (instance-pinned)") {
    CHECK(!std::is_copy_constructible_v<StyleRegistry>);
    CHECK(!std::is_copy_assignable_v<StyleRegistry>);
    CHECK(!std::is_move_constructible_v<StyleRegistry>);
    CHECK(!std::is_move_assignable_v<StyleRegistry>);
}

TEST_CASE("Authoring/StyleRegistry: clear() empties all entries") {
    StyleRegistry reg;
    TextStyle s;
    s.size = 48.0f;
    reg.register_style("a", s).register_style("b", s);
    CHECK(reg.count() == 2);
    reg.clear();
    CHECK(reg.count() == 0);
    CHECK(reg.has("a") == false);
    CHECK(reg.has("b") == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// MotionRegistry equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/MotionRegistry: empty registry resolves nullopt") {
    MotionRegistry reg;
    CHECK(reg.count() == 0);
    CHECK(reg.has("text.reveal.soft") == false);
    CHECK(reg.resolve("text.reveal.soft") == std::nullopt);
}

TEST_CASE("Authoring/MotionRegistry: register_motion resolves TextAnimatorSpec") {
    MotionRegistry reg;
    TextAnimatorSpec preset;
    preset.id = "reveal.soft.preset";
    preset.enabled = true;
    preset.transform_mode = TextPropertyBlendMode::Add;
    preset.color_mode = TextPropertyBlendMode::Replace;
    preset.properties.emplace_back(OpacityProperty{0.0f});
    preset.properties.emplace_back(BlurProperty{12.0f});
    reg.register_motion("text.reveal.soft", preset);

    CHECK(reg.count() == 1);
    CHECK(reg.has("text.reveal.soft"));
    auto resolved = reg.resolve("text.reveal.soft");
    REQUIRE(resolved.has_value());
    CHECK(resolved->id == "reveal.soft.preset");
    CHECK(resolved->enabled == true);
    REQUIRE(resolved->properties.size() == 2);

    bool has_opacity = false;
    bool has_blur = false;
    for (const auto& p : resolved->properties) {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, chronon3d::OpacityProperty>) {
                has_opacity = (v.value.evaluate(0) == doctest::Approx(0.0f));
            } else if constexpr (std::is_same_v<T, chronon3d::BlurProperty>) {
                has_blur = (v.radius.evaluate(0) == doctest::Approx(12.0f));
            }
        }, p);
    }
    CHECK(has_opacity);
    CHECK(has_blur);
}

TEST_CASE("Authoring/MotionRegistry: register_factory produces fresh TextAnimatorSpec per call") {
    MotionRegistry reg;
    int call_count = 0;
    reg.register_factory("parametric.motion", [&call_count]() -> TextAnimatorSpec {
        ++call_count;
        TextAnimatorSpec s;
        s.id = std::string{"call."} + std::to_string(call_count);
        return s;
    });

    REQUIRE(call_count == 0);
    auto r1 = reg.resolve("parametric.motion");
    REQUIRE(r1.has_value());
    CHECK(r1->id == "call.1");
    CHECK(call_count == 1);
    auto r2 = reg.resolve("parametric.motion");
    REQUIRE(r2.has_value());
    CHECK(r2->id == "call.2");
    CHECK(call_count == 2);
}

TEST_CASE("Authoring/MotionRegistry: unregister + has + count API") {
    MotionRegistry reg;
    TextAnimatorSpec preset;
    preset.id = "x";
    reg.register_motion("a", preset).register_motion("b", preset);
    CHECK(reg.count() == 2);
    CHECK(reg.unregister("a") == true);
    CHECK(reg.has("a") == false);
    CHECK(reg.has("b") == true);
    CHECK(reg.count() == 1);
}

TEST_CASE("Authoring/MotionRegistry: empty-id and null-factory are no-ops") {
    MotionRegistry reg;
    TextAnimatorSpec preset;
    preset.id = "x";
    reg.register_motion({}, preset);
    reg.register_factory("valid", nullptr);
    CHECK(reg.count() == 0);
    reg.register_motion("ok", preset);
    CHECK(reg.count() == 1);
}

TEST_CASE("Authoring/MotionRegistry: copy / move forbidden (instance-pinned)") {
    CHECK(!std::is_copy_constructible_v<MotionRegistry>);
    CHECK(!std::is_copy_assignable_v<MotionRegistry>);
    CHECK(!std::is_move_constructible_v<MotionRegistry>);
    CHECK(!std::is_move_assignable_v<MotionRegistry>);
}

TEST_CASE("Authoring/MotionRegistry: composition with StyleRegistry side-by-side") {
    StyleRegistry styles;
    MotionRegistry motions;
    TextStyle hero;
    hero.size = 64.0f;
    hero.font_path = "Inter-Bold.ttf";
    styles.register_style("hero", hero);
    TextAnimatorSpec reveal;
    reveal.id = "reveal";
    reveal.properties.emplace_back(OpacityProperty{0.0f});
    motions.register_motion("reveal.soft", reveal);

    auto s = styles.resolve("hero");
    REQUIRE(s.has_value());
    CHECK(s->size == doctest::Approx(64.0f));
    auto m = motions.resolve("reveal.soft");
    REQUIRE(m.has_value());
    CHECK(m->id == "reveal");
    CHECK(styles.count() == 1);
    CHECK(motions.count() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// register_range + merge tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Registry: register_range bulk loads (string, Value) pairs") {
    StyleRegistry reg;
    const std::array<std::pair<std::string, chronon3d::TextStyle>, 3> pairs{{
        {"a.64", chronon3d::TextStyle{.size = 64.0f, .color = Color{1.0f, 0.0f, 0.0f, 1.0f}}},
        {"b.96", chronon3d::TextStyle{.size = 96.0f, .color = Color{0.0f, 1.0f, 0.0f, 1.0f}}},
        {"c.32", chronon3d::TextStyle{.size = 32.0f, .color = Color{0.0f, 0.0f, 1.0f, 1.0f}}},
    }};
    reg.register_range(pairs.begin(), pairs.end());
    CHECK(reg.count() == 3);
    CHECK(reg.has("a.64"));
    CHECK(reg.has("b.96"));
    CHECK(reg.has("c.32"));
    auto a = reg.resolve("a.64"); REQUIRE(a.has_value()); CHECK(a->size == doctest::Approx(64.0f));
    auto b = reg.resolve("b.96"); REQUIRE(b.has_value()); CHECK(b->size == doctest::Approx(96.0f));
    auto c = reg.resolve("c.32"); REQUIRE(c.has_value()); CHECK(c->size == doctest::Approx(32.0f));
}

TEST_CASE("Authoring/Registry: register_range bulk loads (string, Factory) callables") {
    StyleRegistry reg;
    int call_a = 0;
    int call_b = 0;
    const std::array<std::pair<std::string, std::function<chronon3d::TextStyle()>>, 2> factories{{
        {"param.a", [&]() { ++call_a; chronon3d::TextStyle s; s.size = 24.0f * call_a; return s; }},
        {"param.b", [&]() { ++call_b; chronon3d::TextStyle s; s.size = 12.0f * call_b; return s; }},
    }};
    reg.register_range(factories.begin(), factories.end());
    REQUIRE(call_a == 0);
    REQUIRE(call_b == 0);
    auto a1 = reg.resolve("param.a"); CHECK(call_a == 1); REQUIRE(a1.has_value()); CHECK(a1->size == doctest::Approx(24.0f));
    auto a2 = reg.resolve("param.a"); CHECK(call_a == 2); REQUIRE(a2.has_value()); CHECK(a2->size == doctest::Approx(48.0f));
    auto b1 = reg.resolve("param.b"); CHECK(call_b == 1); REQUIRE(b1.has_value()); CHECK(b1->size == doctest::Approx(12.0f));
}

TEST_CASE("Authoring/Registry: register_range silently skips empty ids") {
    StyleRegistry reg;
    const std::array<std::pair<std::string, chronon3d::TextStyle>, 2> pairs{{
        {"valid", chronon3d::TextStyle{.size = 24.0f}},
        {"", chronon3d::TextStyle{.size = 999.0f}},
    }};
    reg.register_range(pairs.begin(), pairs.end());
    CHECK(reg.count() == 1);
    CHECK(reg.has("valid"));
    CHECK_FALSE(reg.has(""));
}

TEST_CASE("Authoring/Registry: register_range also available on MotionRegistry via inheritance") {
    MotionRegistry reg;
    const std::array<std::pair<std::string, chronon3d::TextAnimatorSpec>, 2> pairs{{
        {"hero.in", chronon3d::TextAnimatorSpec{.id = "hero.in", .enabled = true}},
        {"hero.out", chronon3d::TextAnimatorSpec{.id = "hero.out", .enabled = true}},
    }};
    reg.register_range(pairs.begin(), pairs.end());
    CHECK(reg.count() == 2);
    CHECK(reg.has("hero.in"));
    CHECK(reg.has("hero.out"));
}

TEST_CASE("Authoring/Registry: merge composes builtin pack into target (layer-cake pattern)") {
    StyleRegistry builtin_pack;
    builtin_pack.register_style("youtube.hero.premium",
        chronon3d::TextStyle{.font_path = "Inter-Bold.ttf", .size = 96.0f});
    builtin_pack.register_style("news.lower-third",
        chronon3d::TextStyle{.font_path = "DMSans-Bold.ttf", .size = 48.0f});

    StyleRegistry studio_pack;
    studio_pack.register_style("youtube.hero.premium",
        chronon3d::TextStyle{.font_path = "Anton.ttf", .size = 120.0f});
    studio_pack.register_style("studio.brand.premium",
        chronon3d::TextStyle{.font_path = "Poppins-Bold.ttf", .size = 88.0f});
    studio_pack.merge(builtin_pack);

    CHECK(studio_pack.count() == 3);
    CHECK(studio_pack.has("youtube.hero.premium"));
    CHECK(studio_pack.has("news.lower-third"));
    CHECK(studio_pack.has("studio.brand.premium"));
    auto yh = studio_pack.resolve("youtube.hero.premium");
    REQUIRE(yh.has_value());
    CHECK(yh->font_path == "Inter-Bold.ttf");
    CHECK(yh->size == doctest::Approx(96.0f));
    auto brand = studio_pack.resolve("studio.brand.premium");
    REQUIRE(brand.has_value());
    CHECK(brand->font_path == "Poppins-Bold.ttf");
}

TEST_CASE("Authoring/Registry: merge proxies source — source mutations visible to target") {
    StyleRegistry source;
    StyleRegistry target;
    target.merge(source);
    CHECK(target.count() == 0);
    source.register_style("late.arrival",
        chronon3d::TextStyle{.font_path = "Anton.ttf", .size = 64.0f});
    target.merge(source);
    CHECK(target.count() == 1);
    auto r = target.resolve("late.arrival");
    REQUIRE(r.has_value());
    CHECK(r->font_path == "Anton.ttf");
    CHECK(r->size == doctest::Approx(64.0f));
}

TEST_CASE("Authoring/Registry: merge self-merge is a no-op (no deadlock against std::lock)") {
    StyleRegistry reg;
    reg.register_style("a", chronon3d::TextStyle{.size = 24.0f});
    reg.merge(reg);
    CHECK(reg.count() == 1);
    CHECK(reg.has("a"));
}

TEST_CASE("Authoring/Registry: merge from empty source is a valid no-op") {
    StyleRegistry source;
    StyleRegistry target;
    target.register_style("preexisting", chronon3d::TextStyle{.size = 32.0f});
    target.merge(source);
    CHECK(target.count() == 1);
    CHECK(target.has("preexisting"));
}

TEST_CASE("Authoring/Registry: merge with mixed value+factory source preserves both kinds") {
    StyleRegistry source;
    source.register_style("static.value",
        chronon3d::TextStyle{.font_path = "A.ttf", .size = 24.0f});
    int param_calls = 0;
    source.register_factory("parametric.value", [&]() {
        ++param_calls;
        chronon3d::TextStyle s;
        s.font_path = "B.ttf";
        s.size = 12.0f * param_calls;
        return s;
    });

    StyleRegistry target;
    target.merge(source);
    CHECK(target.count() == 2);
    auto v = target.resolve("static.value");
    REQUIRE(v.has_value());
    CHECK(v->font_path == "A.ttf");
    CHECK(v->size == doctest::Approx(24.0f));
    auto p1 = target.resolve("parametric.value");
    REQUIRE(p1.has_value());
    CHECK(p1->size == doctest::Approx(12.0f));
    CHECK(param_calls == 1);
    auto p2 = target.resolve("parametric.value");
    REQUIRE(p2.has_value());
    CHECK(p2->size == doctest::Approx(24.0f));
    CHECK(param_calls == 2);
}
