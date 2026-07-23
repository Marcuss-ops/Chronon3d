#include "authoring_dsl_test_support.hpp"

using chronon3d::SampleTime;
using chronon3d::authoring::Layer;
using chronon3d::authoring::Text;
using chronon3d::authoring::testing::TextRunBuilderInspector;

namespace {
chronon3d::AssetRegistry& fake_asset_registry() {
    static chronon3d::AssetRegistry registry;
    return registry;
}
} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Ambient registry resolution
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Layer + Text: ambient style(id) resolves via LayerBuilder::extension_context") {
    LayerBuilder lb("ambient_style", SampleTime{});
    StyleRegistry styles;
    TextStyle hero;
    hero.font_path = "fonts/Inter-Bold.ttf";
    hero.font_family = "Inter";
    hero.font_weight = 800;
    hero.size = 96.0f;
    hero.color = Color{1.0f, 0.86f, 0.2f, 1.0f};
    styles.register_style("hero.premium", hero);
    MotionRegistry motions;

    static chronon3d::CompositionRegistry comp;
    static chronon3d::graph::GraphNodeCatalog nodes;
    static chronon3d::effects::EffectCatalog effects;
    chronon3d::ExtensionContext ctx{
        comp, nodes, effects, fake_asset_registry(), &styles, &motions
    };
    lb.extension_context(ctx);
    lb.screen_dimensions(1920.0f, 1080.0f);

    Layer layer(lb);
    Text t = layer.text("AMBIENT");
    REQUIRE(t.ambient_style_registry() == &styles);
    REQUIRE(t.ambient_motion_registry() == &motions);
    t.style("hero.premium");
    CHECK(t.last_style_outcome() == chronon3d::authoring::ResolutionOutcome::Found);

    const auto& spec = TextRunBuilderInspector::pending_of(t)->params.text;
    CHECK(spec.font.font_path == "fonts/Inter-Bold.ttf");
    CHECK(spec.font.font_weight == 800);
    CHECK(spec.font.font_size == doctest::Approx(96.0f));
    CHECK(spec.appearance.color == Color{1.0f, 0.86f, 0.2f, 1.0f});
}

TEST_CASE("Authoring/Layer + Text: ambient motion(id) resolves via LayerBuilder::extension_context") {
    LayerBuilder lb("ambient_motion", SampleTime{});
    MotionRegistry motions;
    TextAnimatorSpec preset;
    preset.id = "text.reveal.soft";
    preset.enabled = true;
    preset.properties.emplace_back(OpacityProperty{0.0f});
    motions.register_motion("text.reveal.soft", preset);
    StyleRegistry styles;

    static chronon3d::CompositionRegistry comp;
    static chronon3d::graph::GraphNodeCatalog nodes;
    static chronon3d::effects::EffectCatalog effects;
    chronon3d::ExtensionContext ctx{
        comp, nodes, effects, fake_asset_registry(), &styles, &motions
    };
    lb.extension_context(ctx);
    lb.screen_dimensions(1920.0f, 1080.0f);

    Layer layer(lb);
    Text t = layer.text("MOTION");
    REQUIRE(t.ambient_motion_registry() == &motions);
    t.motion("text.reveal.soft");
    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.animators.size() == 1);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators[0].id == "text.reveal.soft");
}

TEST_CASE("Authoring/Layer + Text: ambient methods no-op when no ExtensionContext attached") {
    LayerBuilder lb("no_ambient", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("PLAIN");
    t.font("X.ttf", 32.0f);
    REQUIRE(t.ambient_style_registry() == nullptr);
    REQUIRE(t.ambient_motion_registry() == nullptr);

    t.style("ignored");
    CHECK(t.last_style_outcome() == chronon3d::authoring::ResolutionOutcome::RegistryUnavailable);
    t.motion("ignored.motion");
    CHECK(t.last_motion_outcome() == chronon3d::authoring::ResolutionOutcome::RegistryUnavailable);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "X.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators.empty());
}

TEST_CASE("Authoring/Layer + Text: ambient method no-op when ExtensionContext.style_registry is null") {
    LayerBuilder lb("null_style", SampleTime{});
    MotionRegistry motions;
    static chronon3d::CompositionRegistry comp;
    static chronon3d::graph::GraphNodeCatalog nodes;
    static chronon3d::effects::EffectCatalog effects;
    chronon3d::ExtensionContext ctx{
        comp, nodes, effects, fake_asset_registry(), nullptr, &motions
    };
    lb.extension_context(ctx);
    lb.screen_dimensions(1920.0f, 1080.0f);

    Layer layer(lb);
    Text t = layer.text("X");
    t.font("Y.ttf", 24.0f);
    REQUIRE(t.ambient_style_registry() == nullptr);
    REQUIRE(t.ambient_motion_registry() == &motions);
    t.style("anything");
    CHECK(t.last_style_outcome() == chronon3d::authoring::ResolutionOutcome::RegistryUnavailable);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "Y.ttf");
    t.motion("unregistered.id");
    CHECK(t.last_motion_outcome() == chronon3d::authoring::ResolutionOutcome::Missing);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators.empty());
}

TEST_CASE("Authoring/Layer + Text: dual-path coexist (explicit + ambient on the same handle)") {
    LayerBuilder lb("dual", SampleTime{});
    StyleRegistry styles;
    TextStyle ambient_style;
    ambient_style.font_path = "ambient.ttf";
    ambient_style.size = 48.0f;
    TextStyle explicit_style;
    explicit_style.font_path = "explicit.ttf";
    explicit_style.size = 64.0f;
    styles.register_style("ambient_call", ambient_style);

    StyleRegistry explicit_registry;
    explicit_registry.register_style("override", explicit_style);

    static chronon3d::CompositionRegistry comp;
    static chronon3d::graph::GraphNodeCatalog nodes;
    static chronon3d::effects::EffectCatalog effects;
    chronon3d::ExtensionContext ctx{
        comp, nodes, effects, fake_asset_registry(), &styles, nullptr
    };
    lb.extension_context(ctx);
    lb.screen_dimensions(1920.0f, 1080.0f);

    Layer layer(lb);
    Text t = layer.text("DUAL");
    REQUIRE(t.ambient_style_registry() == &styles);
    t.style("ambient_call");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "ambient.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_size == doctest::Approx(48.0f));
    t.style("override", explicit_registry);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "explicit.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_size == doctest::Approx(64.0f));
}

TEST_CASE("Authoring/Layer + Text: ambient resolves unknown id to no-op") {
    LayerBuilder lb("unknown_id", SampleTime{});
    StyleRegistry styles;
    MotionRegistry motions;
    static chronon3d::CompositionRegistry comp;
    static chronon3d::graph::GraphNodeCatalog nodes;
    static chronon3d::effects::EffectCatalog effects;
    chronon3d::ExtensionContext ctx{
        comp, nodes, effects, fake_asset_registry(), &styles, &motions
    };
    lb.extension_context(ctx);
    lb.screen_dimensions(1920.0f, 1080.0f);

    Layer layer(lb);
    Text t = layer.text("UNCHANGED");
    t.font("K.ttf", 24.0f);
    t.style("never.registered");
    t.motion("never.registered.either");
    CHECK(t.last_style_outcome() == chronon3d::authoring::ResolutionOutcome::Missing);
    CHECK(t.last_motion_outcome() == chronon3d::authoring::ResolutionOutcome::Missing);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "K.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators.empty());
}
