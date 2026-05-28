#include <doctest/doctest.h>
#include <chronon3d/presets/phrase/phrase_group_builder.hpp>
#include <chronon3d/presets/style_kit.hpp>
#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/presets/motion_resolver.hpp>

using namespace chronon3d;
using namespace chronon3d::presets;
using namespace chronon3d::presets::phrase;

TEST_CASE("PhraseTheme Factory Properties") {
    auto doc = PhraseTheme::documentary();
    CHECK(doc.radius == doctest::Approx(4.0f));
    CHECK(doc.font_family == "Inter");
    CHECK(doc.panel.a == doctest::Approx(0.9f));

    auto tech_theme = PhraseTheme::tech();
    CHECK(tech_theme.radius == doctest::Approx(16.0f));
    CHECK(tech_theme.accent == Color{0.0f, 0.8f, 1.0f, 1.0f});

    auto luxury_theme = PhraseTheme::luxury();
    CHECK(luxury_theme.radius == doctest::Approx(0.0f));

    auto warning_theme = PhraseTheme::warning();
    CHECK(warning_theme.panel.r == doctest::Approx(0.15f));

    auto sports_theme = PhraseTheme::sports();
    CHECK(sports_theme.panel.g == doctest::Approx(0.15f));

    auto gossip_theme = PhraseTheme::gossip();
    CHECK(gossip_theme.radius == doctest::Approx(24.0f));

    auto breaking_news_theme = PhraseTheme::breaking_news();
    CHECK(breaking_news_theme.radius == doctest::Approx(0.0f));
}

TEST_CASE("PhraseParams apply_theme") {
    PhraseParams p;
    auto initial_radius = p.corner_radius;

    auto warning_theme = PhraseTheme::warning();
    p.apply_theme(warning_theme);

    CHECK(p.corner_radius == doctest::Approx(12.0f));
    CHECK(p.panel_color == warning_theme.panel);
    CHECK(p.accent_color == warning_theme.accent);
    CHECK(p.text_color == warning_theme.title);
    CHECK(p.subtitle_color == warning_theme.subtitle);
}

TEST_CASE("PhraseGroupBuilder Subtitle Omission and Ordering") {
    SUBCASE("Omitting subtitle correctly omits child object") {
        PhraseParams params;
        params.text = "Chronon3d Title";
        params.subtitle = ""; // Empty subtitle!

        PhraseGroupBuilder builder(params);
        builder.panel("bg", MotionPreset::PopIn)
               .title("title", MotionPreset::FadeIn, {0,0,0}, {100,100})
               .subtitle("sub", MotionPreset::FadeIn, {0,50,0}, {100,50});

        const auto& children = builder.get_children();
        REQUIRE(children.size() == 2);
        CHECK(children[0].id == "bg");
        CHECK(children[1].id == "title");
    }

    SUBCASE("Providing subtitle includes child object") {
        PhraseParams params;
        params.text = "Chronon3d Title";
        params.subtitle = "Some Subtitle";

        PhraseGroupBuilder builder(params);
        builder.panel("bg", MotionPreset::PopIn)
               .title("title", MotionPreset::FadeIn, {0,0,0}, {100,100})
               .subtitle("sub", MotionPreset::FadeIn, {0,50,0}, {100,50});

        const auto& children = builder.get_children();
        REQUIRE(children.size() == 3);
        CHECK(children[0].id == "bg");
        CHECK(children[1].id == "title");
        CHECK(children[2].id == "sub");
    }

    SUBCASE("Deterministic child ordering") {
        PhraseParams params;
        params.text = "Main Title";
        params.subtitle = "Subtitle here";

        PhraseGroupBuilder builder(params);
        builder.panel("bg", MotionPreset::PopIn)
               .accent("acc", MotionPreset::SlideLeft, {0,0,0}, {10,10})
               .title("t", MotionPreset::FadeIn, {0,0,0}, {100,50})
               .subtitle("sub", MotionPreset::FadeIn, {0,40,0}, {100,30})
               .quote("q", MotionPreset::PopGlow, {10,10,0});

        const auto& children = builder.get_children();
        REQUIRE(children.size() == 5);
        CHECK(children[0].id == "bg");
        CHECK(children[1].id == "acc");
        CHECK(children[2].id == "t");
        CHECK(children[3].id == "sub");
        CHECK(children[4].id == "q");
    }
}

TEST_CASE("StyleKit Preset Factories and Defaults") {
    auto kit_doc = StyleKit::documentary();
    CHECK(kit_doc.corner_radius == doctest::Approx(4.0f));
    CHECK(kit_doc.typography.font_family == "Inter");
    CHECK(kit_doc.colors.background.a == doctest::Approx(0.9f));

    auto kit_tech = StyleKit::tech();
    CHECK(kit_tech.corner_radius == doctest::Approx(16.0f));
    CHECK(kit_tech.colors.accent == Color{0.0f, 0.8f, 1.0f, 1.0f});

    auto kit_lux = StyleKit::luxury();
    CHECK(kit_lux.corner_radius == doctest::Approx(0.0f));
    CHECK(kit_lux.typography.font_family == "Cinzel");
}

TEST_CASE("MotionPresetRegistry Dynamic Registration and Evaluation") {
    auto& registry = motion::MotionPresetRegistry::instance();

    // 1. Verify standard built-in preset is registered
    CHECK(registry.contains(motion::MotionPreset::PopIn));
    CHECK(registry.get(motion::MotionPreset::PopIn).name == "PopIn");

    // 2. Register custom callback for MotionPreset::None to test dynamic resolution override
    bool custom_called = false;
    registry.register_preset({
        motion::MotionPreset::None,
        "CustomNone",
        [&](const FrameContext&, const motion::MotionObject&, f32, motion::MotionState& st) {
            custom_called = true;
            st.opacity = 0.42f;
        }
    });

    motion::MotionObject obj;
    obj.preset(motion::MotionPreset::None);

    FrameContext ctx;
    ctx.frame = 10;
    ctx.frame_rate = {60, 1};

    auto state = motion::resolve_motion_state(ctx, obj);
    CHECK(custom_called == true);
    CHECK(state.opacity == doctest::Approx(0.42f));

    // 3. Restore standard None resolver to avoid breaking downstream pipeline tests
    registry.register_preset({
        motion::MotionPreset::None,
        "None",
        [](const FrameContext&, const motion::MotionObject&, f32, motion::MotionState&) {}
    });
}

