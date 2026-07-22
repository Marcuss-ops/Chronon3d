#include <doctest/doctest.h>
#include <chronon3d/presets/phrase/phrase_group_builder.hpp>
#include <chronon3d/presets/style_kit.hpp>
#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/presets/motion_resolver.hpp>

using namespace chronon3d::presets;
using namespace chronon3d::presets::motion;
using namespace chronon3d::presets::phrase;

using chronon3d::FrameContext;
using chronon3d::f32;
using chronon3d::Easing;
using chronon3d::EasingCurve;
using chronon3d::video::VideoSource;
using namespace chronon3d;

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
    auto& registry = MotionPresetRegistry::instance();

    // 1. Verify standard built-in preset is registered
    CHECK(registry.contains(MotionPreset::PopIn));
    CHECK(registry.get(MotionPreset::PopIn).name == "PopIn");

    // 2. Register custom callback for MotionPreset::None to test dynamic resolution override
    bool custom_called = false;
    registry.register_preset({
        MotionPreset::None,
        "CustomNone",
        [&](const FrameContext&, const MotionObject&, f32, MotionState& st) {
            custom_called = true;
            st.opacity = 0.42f;
        }
    });

    MotionObject obj;
    obj.preset(MotionPreset::None);

    FrameContext ctx;
    ctx = ctx.with_frame(10);
    ctx = ctx.with_frame_rate({60, 1});

    auto state = resolve_motion_state(ctx, obj);
    CHECK(custom_called == true);
    CHECK(state.opacity == doctest::Approx(0.42f));

    // 3. Restore standard None resolver to avoid breaking downstream pipeline tests
    registry.register_preset({
        MotionPreset::None,
        "None",
        [](const FrameContext&, const MotionObject&, f32, MotionState&) {}
    });
}

TEST_CASE("MotionStyle and unified effects resolving") {
    GlowParams glow_p{.radius = 45.0f, .intensity = 0.9f};
    DropShadowParams shadow_p{.offset = {0, 18}, .radius = 24.0f};
    BloomParams bloom_p{.threshold = 0.5f, .radius = 12.0f};

    auto obj = MotionObject::text("test_obj", "Testing Style")
        .glow(glow_p)
        .shadow(shadow_p)
        .bloom(bloom_p)
        .blend(BlendMode::Screen)
        .cache_static(true);

    CHECK(obj.style.glow_enabled == true);
    CHECK(obj.style.glow.radius == doctest::Approx(45.0f));
    CHECK(obj.style.shadow_enabled == true);
    CHECK(obj.style.shadow.radius == doctest::Approx(24.0f));
    CHECK(obj.style.bloom_enabled == true);
    CHECK(obj.style.bloom.threshold == doctest::Approx(0.5f));
    CHECK(obj.style.blend == BlendMode::Screen);
    CHECK(obj.style.cache_static == true);

    FrameContext ctx;
    ctx = ctx.with_frame(10);
    ctx = ctx.with_frame_rate({60, 1});
    auto state = resolve_motion_state(ctx, obj);

    CHECK(state.effects.glow_enabled == true);
    CHECK(state.effects.glow.radius == doctest::Approx(45.0f));
    CHECK(state.effects.shadow_enabled == true);
    CHECK(state.effects.shadow.radius == doctest::Approx(24.0f));
    CHECK(state.effects.bloom_enabled == true);
    CHECK(state.effects.bloom.threshold == doctest::Approx(0.5f));
}

TEST_CASE("Video and Stock MotionObject + 3D presets resolving") {
    VideoSource source{.path = "test.mp4", .size = {1920.0f, 1080.0f}};
    auto v_obj = MotionObject::video("video_test", source)
        .preset(MotionPreset::CinematicPushIn);

    CHECK(v_obj.type == MotionObjectType::Video);
    CHECK(v_obj.video_source_value.path == "test.mp4");
    CHECK(v_obj.size_value.x == doctest::Approx(1920.0f));

    auto s_obj = MotionObject::stock("stock_test", "tag_name")
        .preset(MotionPreset::ParallaxFloat);

    CHECK(s_obj.type == MotionObjectType::Stock);
    CHECK(s_obj.stock_tag_value == "tag_name");

    FrameContext ctx;
    ctx = ctx.with_frame(15);
    ctx = ctx.with_frame_rate({60, 1});

    auto state = resolve_motion_state(ctx, v_obj);
    CHECK(state.visible == true);
    CHECK(state.position.z != doctest::Approx(0.0f));
}

#include <chronon3d/presets/text/text_style_presets.hpp>

TEST_CASE("TextStyle presets return correctly configured styles") {
    auto headline_style = presets::text::headline();
    CHECK(headline_style.font_family == "Inter");
    CHECK(headline_style.font_weight == 900);
    CHECK(headline_style.paint.stroke_enabled == true);

    auto subtitle_style = presets::text::subtitle();
    CHECK(subtitle_style.box_style.enabled == true);
    CHECK(subtitle_style.box_style.radius == doctest::Approx(8.0f));

    auto neon_style = presets::text::neon();
    CHECK(neon_style.shadows.size() == 3);

}

#include <chronon3d/presets/text/subtitle.hpp>

TEST_CASE("Subtitle and Karaoke models specification") {
    presets::text::SubtitleTrack track;
    presets::text::SubtitleCue cue;
    cue.start = 0;
    cue.end = 60;
    cue.text = "Hello World";
    
    presets::text::WordTiming w1{.word = "Hello", .start = 0, .end = 30};
    presets::text::WordTiming w2{.word = "World", .start = 30, .end = 60};
    cue.words.push_back(w1);
    cue.words.push_back(w2);
    
    track.cues.push_back(cue);
    
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].text == "Hello World");
    REQUIRE(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].words[1].word == "World");
}
