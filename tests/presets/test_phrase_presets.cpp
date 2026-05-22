#include <doctest/doctest.h>

#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/motion_resolver.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>

using namespace chronon3d;
using namespace chronon3d::presets::motion;
using namespace chronon3d::presets::phrase;
using namespace chronon3d::test;

static const MotionObject* find_child(const MotionObject& group, const char* id) {
    for (const auto& child : group.children) {
        if (child.id == id) {
            return &child;
        }
    }
    return nullptr;
}

TEST_CASE("TitlePop phrase builds a stacked motion group") {
    const PhraseParams params{
        .id = "headline",
        .text = "THIS CHANGES EVERYTHING",
        .subtitle = "A headline that lands hard",
        .start = 0,
        .end = 90,
    };

    const MotionObject phrase = make_phrase(PhrasePreset::TitlePop, params);
    CHECK(phrase.type == MotionObjectType::Group);
    CHECK(phrase.children.size() == 4);

    const MotionObject* bg = find_child(phrase, "bg");
    const MotionObject* title = find_child(phrase, "title");
    const MotionObject* subtitle = find_child(phrase, "subtitle");
    REQUIRE(bg != nullptr);
    REQUIRE(title != nullptr);
    REQUIRE(subtitle != nullptr);

    CHECK(bg->preset_value == MotionPreset::PopIn);
    CHECK(title->preset_value == MotionPreset::PopGlow);
    CHECK(subtitle->preset_value == MotionPreset::FadeIn);
    CHECK(title->glow_enabled);
}

TEST_CASE("WarningPulse phrase shakes the title over time") {
    const PhraseParams params{
        .id = "warning",
        .text = "BREAKING",
        .subtitle = "Motion preset: warning pulse",
        .start = 0,
        .end = 90,
    };

    const MotionObject phrase = make_phrase(PhrasePreset::WarningPulse, params);
    const MotionObject* title = find_child(phrase, "warning");
    REQUIRE(title != nullptr);

    const MotionState start = resolve_motion_state(make_ctx(0), *title);
    const MotionState mid = resolve_motion_state(make_ctx(30), *title);

    CHECK(start.visible);
    CHECK(mid.visible);
    CHECK(std::abs(start.position.x - mid.position.x) > 0.1f);
}

TEST_CASE("TypewriterCaption phrase reveals text progressively") {
    const PhraseParams params{
        .id = "typewriter",
        .text = "TYPEWRITER CAPTION",
        .subtitle = "Characters appear progressively",
        .start = 0,
        .end = 90,
    };

    const MotionObject phrase = make_phrase(PhrasePreset::TypewriterCaption, params);
    const MotionObject* title = find_child(phrase, "title");
    REQUIRE(title != nullptr);

    const MotionState start = resolve_motion_state(make_ctx(0), *title);
    const MotionState end = resolve_motion_state(make_ctx(90), *title);

    CHECK(start.text_reveal < 0.2f);
    CHECK(end.text_reveal > 0.95f);
}

TEST_CASE("BounceTitle phrase uses kinetic bounce") {
    const PhraseParams params{
        .id = "bounce",
        .text = "BOUNCE TITLE",
        .subtitle = "Kinetic pop-in",
        .start = 0,
        .end = 90,
    };

    const MotionObject phrase = make_phrase(PhrasePreset::BounceTitle, params);
    const MotionObject* title = find_child(phrase, "title");
    REQUIRE(title != nullptr);

    CHECK(title->preset_value == MotionPreset::KineticBounce);
}

TEST_CASE("GlitchBanner phrase uses glitch motion") {
    const PhraseParams params{
        .id = "glitch",
        .text = "GLITCH BANNER",
        .subtitle = "Short burst of instability",
        .start = 0,
        .end = 90,
    };

    const MotionObject phrase = make_phrase(PhrasePreset::GlitchBanner, params);
    const MotionObject* title = find_child(phrase, "title");
    REQUIRE(title != nullptr);

    CHECK(title->preset_value == MotionPreset::GlitchIn);
}

TEST_CASE("draw_phrase builds visible layers for captions") {
    const PhraseParams params{
        .id = "caption",
        .text = "A clean caption style for narration",
        .subtitle = "minimal, readable, and calm",
        .start = 0,
        .end = 90,
        .position = {0.0f, 0.0f, 0.0f},
    };

    const auto ctx = make_ctx(30);
    SceneBuilder s(ctx);
    draw_phrase(s, ctx, PhrasePreset::CaptionClean, params);

    const Scene scene = s.build();
    CHECK(scene.layers().size() >= 2);
}
