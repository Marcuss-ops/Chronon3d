#include <doctest/doctest.h>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <string>
#include <vector>

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_TEXT) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include <content/register_content_modules.hpp>
#endif

using namespace chronon3d;

// ── Smoke tests: each composition creates and evaluates at frame 0 without crashing ──

#ifdef CHRONON3D_HAS_CONTENT_MINIMALIST

TEST_CASE("Minimalist: all text presets evaluate frame 0") {
    register_minimalist_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "MinimalistFadeIn", "MinimalistFadeShiftVertical",
        "MinimalistFadeShiftHorizontal", "MinimalistFocusIn",
        "MinimalistRevealFromBottom", "MinimalistCenterSplit",
        "MinimalistUnderlineDraw", "MinimalistHighlightBlock",
        "MinimalistFramingBracket", "MinimalistWordStagger",
        "MinimalistScaleDrop", "MinimalistTrackingBreathing",
        "MinimalistElegantExitVertical", "MinimalistElegantExitHorizontal",
        "MinimalistCurtainClose"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        CHECK(comp.width() > 0);
        CHECK(comp.height() > 0);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

TEST_CASE("Minimalist: all image presets evaluate frame 0") {
    register_minimalist_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "MinimalistImageFadeIn", "MinimalistImageFocusIn",
        "MinimalistImageScaleDrop", "MinimalistImageFadeShiftVertical",
        "MinimalistImageCenterSplit", "MinimalistImageRevealFromBottom",
        "MinimalistImageFramingBracket", "MinimalistImageTrackingBreathing",
        "MinimalistImageElegantExit", "MinimalistImageElasticSlide"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

TEST_CASE("Minimalist: showcase compositions evaluate frame 0") {
    register_minimalist_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "MinimalistFocusQuote", "MinimalistPhraseFloat",
        "MinimalistHeroExplainer", "MinimalistScaleExplainer",
        "MinimalistCleanQuote"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

#endif // CHRONON3D_HAS_CONTENT_MINIMALIST

#ifdef CHRONON3D_HAS_CONTENT_TEXT

TEST_CASE("Text: core text presets evaluate frame 0") {
    register_text_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "TextCreditRoll", "TextSplitScreen", "TextGlow", "TextShadow",
        "TextPulse", "TextMultiStyle", "TextOnBackground", "TextGridOverlay"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

TEST_CASE("Text: typewriter variants evaluate frame 0") {
    register_text_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "TextTypewriter", "TextTypewriterTerminal",
        "TextTypewriterQuote", "TextTypewriterManifest",
        "TextTypewriterChapter", "TextTypewriterShowcase"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

TEST_CASE("Text: premium hero compositions evaluate frame 0") {
    register_text_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "HeroFresh", "TextPremiumHero", "TextPremiumHeroSaaSBlue"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

TEST_CASE("Text: utility compositions evaluate frame 0") {
    register_text_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "Empty", "TextOnly"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 0);
    }
}

#endif // CHRONON3D_HAS_CONTENT_TEXT

#ifdef CHRONON3D_HAS_CONTENT_2D5

TEST_CASE("2D5: core 2.5D scenes evaluate frame 0") {
    register_two_point_five_d_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "ParallaxSimple", "DepthScene", "CardFlip", "DofShowcase"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

TEST_CASE("2D5: camera test compositions evaluate frame 0") {
    register_two_point_five_d_content();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "CameraOrbitTargetLockTest", "CameraDollyPerspectiveScaleTest",
        "CameraParentNullRigTest", "CameraRollPanTiltGridTest"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

#endif // CHRONON3D_HAS_CONTENT_2D5

// ── Cross-module: all available compositions evaluate safely ──────────────────

TEST_CASE("All registered compositions: every available composition evaluates frame 0") {
    CompositionRegistry registry;
    auto ids = registry.available();

    REQUIRE(ids.size() > 0);

    for (const auto& name : ids) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        CHECK(comp.width() > 0);
        CHECK(comp.height() > 0);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 0);
    }
}

TEST_CASE("All registered compositions: evaluate at mid-duration frame") {
    CompositionRegistry registry;
    auto ids = registry.available();

    for (const auto& name : ids) {
        auto comp = registry.create(name);
        Frame mid = comp.duration() / 2;
        auto scene = comp.evaluate(mid);
        CHECK(scene.layers().size() >= 0);
    }
}
