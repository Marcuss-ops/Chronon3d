#include <doctest/doctest.h>

#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/motion_resolver.hpp>

#include <cmath>

using namespace chronon3d;
using namespace chronon3d::presets::motion;
using namespace chronon3d::presets::phrase;

static FrameContext make_ctx(Frame frame) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = FrameRate{30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

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
