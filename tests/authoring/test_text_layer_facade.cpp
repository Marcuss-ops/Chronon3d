#include "authoring_dsl_test_support.hpp"

using chronon3d::CanvasInfo;
using chronon3d::authoring::Layer;
using chronon3d::authoring::Text;
using chronon3d::authoring::testing::TextRunBuilderInspector;

namespace {
CanvasInfo canvas(float width, float height) {
    return CanvasInfo::with_safe_area(width, height, chronon3d::SafeAreaPreset{});
}
} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Text + Layer façade equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Layer: text() pushes a PendingTextRun with auto-generated name") {
    LayerBuilder lb("test_layer", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t1 = layer.text("HELLO");
    Text t2 = layer.text("WORLD");
    CHECK(TextRunBuilderInspector::pending_of(t1).name == "text_0");
    CHECK(TextRunBuilderInspector::pending_of(t2).name == "text_1");
    CHECK(TextRunBuilderInspector::pending_of(t1)->params.text.content.value == "HELLO");
    CHECK(TextRunBuilderInspector::pending_of(t2)->params.text.content.value == "WORLD");
    const auto snap_t1 = TextRunBuilderInspector::pending_of(t1);
    const auto snap_t2 = TextRunBuilderInspector::pending_of(t2);
    CHECK(snap_t1.pending != snap_t2.pending);
}

TEST_CASE("Authoring/Text: id() + content() store and propagate to underlying spec") {
    LayerBuilder lb("id_content", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("initial");
    t.id("hero-title").content("UPDATED");
    CHECK(TextRunBuilderInspector::pending_of(t).name == "hero-title");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.content.value == "UPDATED");
}

TEST_CASE("Authoring/Text: font() / font_family() / weight() / italic() / font_size() cover FontSpec") {
    LayerBuilder lb("font", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("x");
    t.font("assets/fonts/Inter-Bold.ttf", 96.0f)
     .font_family("Inter")
     .weight(800)
     .italic(true)
     .font_size(120.0f);

    const auto& font = TextRunBuilderInspector::pending_of(t)->params.text.font;
    CHECK(font.font_path == "assets/fonts/Inter-Bold.ttf");
    CHECK(font.font_family == "Inter");
    CHECK(font.font_weight == 800);
    CHECK(font.font_style == "italic");
    CHECK(font.font_size == doctest::Approx(120.0f));
}

TEST_CASE("Authoring/Text: at(Vec2) and at(Vec3) store two-dimensional placement") {
    LayerBuilder lb("position", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t_v2 = layer.text("v2");
    t_v2.at(Vec2{100.0f, 200.0f});
    CHECK(TextRunBuilderInspector::pending_of(t_v2)->params.text.placement.offset
          == doctest::Approx2D(Vec2{100.0f, 200.0f}));

    Text t_v3 = layer.text("v3");
    t_v3.at(Vec3{11.0f, 22.0f, 33.0f});
    CHECK(TextRunBuilderInspector::pending_of(t_v3)->params.text.placement.offset
          == doctest::Approx2D(Vec2{11.0f, 22.0f}));

    Text t_2arg = layer.text("2arg");
    t_2arg.at(7.0f, 8.0f);
    CHECK(TextRunBuilderInspector::pending_of(t_2arg)->params.text.placement.offset
          == doctest::Approx2D(Vec2{7.0f, 8.0f}));
}

TEST_CASE("Authoring/Text: center() uses explicit CanvasInfo") {
    LayerBuilder lb("center", SampleTime{});
    Layer layer(lb, canvas(800.0f, 600.0f));
    Text t = layer.text("hero");
    t.center();
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.placement.offset
          == doctest::Approx2D(Vec2{400.0f, 300.0f}));
    const auto& layout = TextRunBuilderInspector::pending_of(t)->params.text.layout;
    CHECK(layout.anchor == TextAnchor::Center);
    CHECK(layout.align == TextAlign::Center);
    CHECK(layout.vertical_align == VerticalAlign::Middle);
}

TEST_CASE("Authoring/Text: center() uses CanvasInfo derived by Layer ctor") {
    LayerBuilder lb("center_fb", SampleTime{});
    lb.screen_dimensions(1280.0f, 720.0f);
    Layer layer(lb);
    Text t = layer.text("x");
    t.center();
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.placement.offset
          == doctest::Approx2D(Vec2{640.0f, 360.0f}));
}

TEST_CASE("Authoring/Text: layout setters propagate to spec.text.layout") {
    LayerBuilder lb("layout_props", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("x");
    t.box({1500.0f, 220.0f})
     .anchor_point(TextAnchor::Center)
     .align(TextAlign::Center)
     .vertical_align(VerticalAlign::Middle)
     .pixel_ink_centering()
     .layout_box_centering()
     .line_height(0.95f)
     .tracking(-1.5f)
     .wrap(TextWrap::Character)
     .overflow(TextOverflow::Ellipsis)
     .ellipsis(true)
     .max_lines(2)
     .auto_fit(48.0f, 2)
     .max_font_size(160.0f);

    const auto& layout = TextRunBuilderInspector::pending_of(t)->params.text.layout;
    CHECK(layout.box == Vec2{1500.0f, 220.0f});
    CHECK(layout.anchor == TextAnchor::Center);
    CHECK(layout.align == TextAlign::Center);
    CHECK(layout.vertical_align == VerticalAlign::Middle);
    CHECK(layout.centering_mode == TextCenteringMode::LayoutBox);
    CHECK(layout.line_height == doctest::Approx(0.95f));
    CHECK(layout.tracking == doctest::Approx(-1.5f));
    CHECK(layout.wrap == TextWrap::Character);
    CHECK(layout.overflow == TextOverflow::Ellipsis);
    CHECK(layout.ellipsis == true);
    CHECK(layout.max_lines == 2);
    CHECK(layout.auto_fit == true);
    CHECK(layout.min_font_size == doctest::Approx(48.0f));
    CHECK(layout.max_font_size == doctest::Approx(160.0f));
}

TEST_CASE("Authoring/Text: color() mutates appearance.color only") {
    LayerBuilder lb("color", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("x");
    t.color(Color{0.9f, 0.1f, 0.2f, 1.0f});
    const auto& appearance = TextRunBuilderInspector::pending_of(t)->params.text.appearance;
    CHECK(appearance.color == Color{0.9f, 0.1f, 0.2f, 1.0f});
    CHECK(appearance.paint.fill == Color{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(appearance.shadows.empty());
    CHECK(appearance.material.enabled == false);
    CHECK(appearance.box_style.enabled == false);
}

TEST_CASE("Authoring/Text: material(Material) consumes Material into appearance.material") {
    LayerBuilder lb("material_consume", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("x");
    Material m = material::premium().bevel(2.0f).glow(8.0f, 0.5f);
    t.material(std::move(m));
    const auto& captured = TextRunBuilderInspector::pending_of(t)->params.text.appearance.material;
    CHECK(captured.enabled == true);
    CHECK(captured.bevel_px == doctest::Approx(2.0f));
    CHECK(captured.use_material_glow == true);
    CHECK(captured.glow_radius == doctest::Approx(8.0f));
    CHECK(captured.glow_intensity == doctest::Approx(0.5f));
}

TEST_CASE("Authoring/Text: animate(Animator) consumes Animator into animators vector") {
    LayerBuilder lb("animate_consume", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("hero");
    Animator a = animator("reveal");
    Selector sel = selector(TextSelectorUnit::Grapheme);
    sel.smooth().start(Frame{0}, 0.0f).start(Frame{24}, 100.0f, Easing::OutCubic);
    a.select(std::move(sel)).position(Vec3{0.0f, 46.0f, 0.0f}).scale(0.94f).opacity(0.0f);
    t.animate(std::move(a));

    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.animators.size() == 1);
    const auto& appended = TextRunBuilderInspector::pending_of(t)->params.animators.back();
    CHECK(appended.id == "reveal");
    CHECK(appended.enabled == true);
    REQUIRE(appended.selectors.size() == 1);
    CHECK(appended.selectors[0].unit == TextSelectorUnit::Grapheme);
    CHECK(appended.selectors[0].shape == TextSelectorShape::Smooth);
    REQUIRE(appended.properties.size() == 3);
}

TEST_CASE("Authoring/Text: multiple animate() calls accumulate in order") {
    LayerBuilder lb("multi_anim", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("x");
    t.animate(animator("in").opacity(0.0f));
    t.animate(animator("out").opacity(1.0f));
    t.animate(animator("idle").tracking(2.0f));
    const auto& list = TextRunBuilderInspector::pending_of(t)->params.animators;
    REQUIRE(list.size() == 3);
    CHECK(list[0].id == "in");
    CHECK(list[1].id == "out");
    CHECK(list[2].id == "idle");
}

TEST_CASE("Authoring/Text: style(id, registry) field-maps TextStyle to spec.text") {
    LayerBuilder lb("style_map", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    StyleRegistry styles;
    TextStyle hero;
    hero.font_path = "assets/fonts/Inter-Bold.ttf";
    hero.font_family = "Inter";
    hero.font_weight = 700;
    hero.font_style = "normal";
    hero.size = 96.0f;
    hero.color = Color{1.0f, 0.86f, 0.2f, 1.0f};
    hero.anchor = TextAnchor::Center;
    hero.align = TextAlign::Center;
    hero.line_height = 1.0f;
    hero.tracking = -1.0f;
    hero.max_lines = 1;
    styles.register_style("youtube.hero.premium", hero);

    Text t = layer.text("CHRONON");
    t.style("youtube.hero.premium", styles);
    CHECK(t.last_style_outcome() == chronon3d::authoring::ResolutionOutcome::Found);
    const auto& font = TextRunBuilderInspector::pending_of(t)->params.text.font;
    const auto& layout = TextRunBuilderInspector::pending_of(t)->params.text.layout;
    const auto& appearance = TextRunBuilderInspector::pending_of(t)->params.text.appearance;
    CHECK(font.font_path == "assets/fonts/Inter-Bold.ttf");
    CHECK(font.font_family == "Inter");
    CHECK(font.font_weight == 700);
    CHECK(font.font_style == "normal");
    CHECK(font.font_size == doctest::Approx(96.0f));
    CHECK(appearance.color == Color{1.0f, 0.86f, 0.2f, 1.0f});
    CHECK(layout.anchor == TextAnchor::Center);
    CHECK(layout.align == TextAlign::Center);
    CHECK(layout.line_height == doctest::Approx(1.0f));
    CHECK(layout.tracking == doctest::Approx(-1.0f));
    CHECK(layout.max_lines == 1);
}

TEST_CASE("Authoring/Text: style() with unknown id is a no-op") {
    LayerBuilder lb("style_nomatch", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("FUTURI MILIONARI");
    t.font("Inter-Bold.ttf", 106.0f).color(Color::white());
    const StyleRegistry empty_registry;
    t.style("not.registered", empty_registry);
    CHECK(t.last_style_outcome() == chronon3d::authoring::ResolutionOutcome::Missing);
    const auto& spec = TextRunBuilderInspector::pending_of(t)->params.text;
    CHECK(spec.content.value == "FUTURI MILIONARI");
    CHECK(spec.font.font_path == "Inter-Bold.ttf");
    CHECK(spec.appearance.color.r == doctest::Approx(1.0f));
}

TEST_CASE("Authoring/Text: motion(id, registry) appends resolved animator") {
    LayerBuilder lb("motion_consume", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    MotionRegistry motions;
    TextAnimatorSpec preset;
    preset.id = "text.reveal.soft";
    preset.enabled = true;
    preset.properties.emplace_back(OpacityProperty{0.0f});
    preset.properties.emplace_back(BlurProperty{12.0f});
    motions.register_motion("text.reveal.soft", preset);
    Text t = layer.text("hero");
    t.font("Inter-Bold.ttf", 106.0f).center().motion("text.reveal.soft", motions);
    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.animators.size() == 1);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators[0].id == "text.reveal.soft");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators[0].properties.size() == 2);
}

TEST_CASE("Authoring/Text: configure_core(Fn) mutates raw TextRunSpec") {
    LayerBuilder lb("configure", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("Anti-Dup");
    t.font("Anton.ttf", 200.0f);
    t.configure_core([](chronon3d::TextRunSpec& p) {
        p.direction = TextDirection::RTL;
        p.language = "ar";
        p.cache_layout = false;
    });
    const auto& params = TextRunBuilderInspector::pending_of(t)->params;
    CHECK(params.direction == TextDirection::RTL);
    CHECK(params.language == "ar");
    CHECK(params.cache_layout == false);
    CHECK(params.text.font.font_path == "Anton.ttf");
}

TEST_CASE("Authoring/Text: end-to-end hero chain is mutator-agnostic to TextRunBuilder") {
    LayerBuilder lb("hero", SampleTime{});
    Layer layer(lb, canvas(1920.0f, 1080.0f));
    layer.text("FUTURI MILIONARI")
        .id("hero-title")
        .font("assets/fonts/Inter-Bold.ttf", 106.0f)
        .center()
        .box({1500.0f, 220.0f})
        .align(TextAlign::Center)
        .vertical_align(VerticalAlign::Middle)
        .pixel_ink_centering()
        .line_height(0.95f)
        .tracking(-1.0f)
        .auto_fit(58.0f, 2)
        .color(Color::white())
        .material(material::premium().bevel(1.5f).glow(14.0f, 0.45f))
        .animate(animator("hero-reveal")
            .select(selector(TextSelectorUnit::Grapheme)
                .smooth().exclude_spaces()
                .start(Frame{0}, 0.0f)
                .start(Frame{24}, 100.0f, Easing::OutCubic))
            .position(Vec3{0.0f, 46.0f, 0.0f})
            .scale(0.94f).opacity(0.0f).blur(12.0f).tracking(10.0f));
    Text probe = layer.text("probe");
    CHECK(TextRunBuilderInspector::pending_of(probe).name == "text_1");
}

TEST_CASE("Authoring/Layer: text() state survives returned handle destruction") {
    LayerBuilder lb("destroy", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    {
        Layer layer(lb);
        Text t = layer.text("ephemeral");
        t.font("X.ttf", 12.0f);
    }
    Layer layer2(lb);
    Text probe = layer2.text("verify");
    CHECK(TextRunBuilderInspector::pending_of(probe).name == "text_0");
}

TEST_CASE("Authoring/Text + Layer: move-only contracts") {
    CHECK(!std::is_copy_constructible_v<Text>);
    CHECK(!std::is_copy_assignable_v<Text>);
    CHECK(std::is_move_constructible_v<Text>);
    CHECK(std::is_move_assignable_v<Text>);
    CHECK(!std::is_copy_constructible_v<Layer>);
    CHECK(!std::is_copy_assignable_v<Layer>);
    CHECK(std::is_move_constructible_v<Layer>);
    CHECK(std::is_move_assignable_v<Layer>);
}

TEST_CASE("Authoring/CanvasInfo: explicit dimensions are preserved") {
    auto info = canvas(640.0f, 480.0f);
    CHECK(info.width == doctest::Approx(640.0f));
    CHECK(info.height == doctest::Approx(480.0f));
}
